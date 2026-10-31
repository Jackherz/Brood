const assert = require('node:assert/strict');
// Optional end-to-end smoke test. Start Vite first and set CHROMIUM_PATH.
const { chromium } = require('playwright-core');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'brood-browser-'));
const stereoPath = path.join(temp, 'brood-stereo-fixture.wav');
const micPath = path.join(temp, 'brood-mic-fixture.wav');
function writeWav(file, channels, rate, seconds) {
 const frames = rate * seconds, pcm = Buffer.alloc(44 + frames * channels * 2);
 pcm.write('RIFF',0); pcm.writeUInt32LE(pcm.length - 8,4); pcm.write('WAVEfmt ',8);
 pcm.writeUInt32LE(16,16); pcm.writeUInt16LE(1,20); pcm.writeUInt16LE(channels,22);
 pcm.writeUInt32LE(rate,24); pcm.writeUInt32LE(rate * channels * 2,28);
 pcm.writeUInt16LE(channels * 2,32); pcm.writeUInt16LE(16,34); pcm.write('data',36); pcm.writeUInt32LE(pcm.length - 44,40);
 for (let i=0; i<frames; i++) for (let ch=0; ch<channels; ch++)
   pcm.writeInt16LE(ch===0 ? Math.round(Math.sin(2*Math.PI*440*i/rate)*12000) : 0, 44 + (i * channels + ch) * 2);
 fs.writeFileSync(file, pcm);
}
writeWav(stereoPath, 2, 44100, 1); writeWav(micPath, 1, 48000, 3);
let browser;
(async () => {
 if (!process.env.CHROMIUM_PATH) throw new Error('Set CHROMIUM_PATH to a Chromium or Chrome executable.');
 browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH, args: ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream', `--use-file-for-fake-audio-capture=${micPath}`, '--no-sandbox', ...(process.env.CHROMIUM_FLAGS || '').split(' ').filter(Boolean)], headless: true });
 const context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, permissions: ['microphone'] });
 const page = await context.newPage(); const errors = [];
 page.on('pageerror', error => errors.push(error.message));
 page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
 await page.goto(process.env.BROOD_TEST_URL || 'http://127.0.0.1:5173/', { waitUntil: 'networkidle' });
 // Exercise the actual browser AudioWorklet offline with a left-only input.
 const stereo = await page.evaluate(async () => {
   async function run(wet) {
     const fs=44100, ctx = new OfflineAudioContext(2, fs * 2, fs);
     await ctx.audioWorklet.addModule('/src/mood-worklet.js');
     const node = new AudioWorkletNode(ctx, 'mood-processor', { numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[2], channelCount:2, channelCountMode:'explicit' });
     let wetSeen = false;
     node.port.onmessage = e => { if (e.data.type === 'state' && e.data.st.wetOn) wetSeen = true; };
     const b = ctx.createBuffer(2, fs*2, fs), left=b.getChannelData(0);
     for(let i=0;i<left.length;i++) left[i]=0.3*Math.sin(2*Math.PI*440*i/fs);
     const s=ctx.createBufferSource();s.buffer=b;s.connect(node);node.connect(ctx.destination);s.start();
     // Wait for the worklet to exist, then send messages while offline rendering
     // is suspended. Sending before startRendering can race a fast renderer.
     const paused = ctx.suspend(0.05), rendering = ctx.startRendering();
     await paused;
     if (wet) {
       for (const [name,value] of Object.entries({wetMode:1, time:0.5, mix:1, dryKill:1, clock:1, modWet:0.4})) node.port.postMessage({type:'param', name, value});
       node.port.postMessage({type:'fs', which:'wet', action:'down'}); node.port.postMessage({type:'fs', which:'wet', action:'up'});
     }
     await new Promise(resolve => setTimeout(resolve, 50));
     await ctx.resume();
     const out=await rendering, l=out.getChannelData(0), r=out.getChannelData(1);
     await new Promise(resolve => setTimeout(resolve, 20));
     let a=0,b2=0;for(let i=fs;i<l.length;i++){a+=l[i]*l[i];b2+=r[i]*r[i];}
     return {left:Math.sqrt(a/fs), right:Math.sqrt(b2/fs), wetSeen};
   }
   return { bypass:await run(false), wet:await run(true) };
 });
 assert(stereo.bypass.left > 0.1 && stereo.bypass.right < 1e-7, JSON.stringify(stereo));
 assert(stereo.wet.wetSeen && stereo.wet.left > 0.01 && stereo.wet.right < 1e-7, JSON.stringify(stereo));
 assert(Math.abs(stereo.wet.left - stereo.bypass.left) > 0.02, 'wet processing changed audio: ' + JSON.stringify(stereo));
 console.log('offline AudioWorklet stereo', stereo);
 await page.getByRole('button',{name:'START AUDIO'}).click();
 await page.getByRole('button',{name:'PAUSE AUDIO'}).waitFor();
 const wet = page.getByRole('button',{name:/Wet channel: tap/});
 const loop = page.getByRole('button',{name:/Micro-looper: tap/});
 await wet.click(); await page.waitForFunction(()=>document.querySelector('.footswitch').getAttribute('aria-pressed')==='true');
 await wet.hover(); await page.mouse.down(); await page.waitForTimeout(700);
 await page.waitForFunction(()=>document.querySelector('.footswitch .foot-state').textContent==='FROZEN');
 await page.mouse.up(); await page.waitForFunction(()=>document.querySelector('.footswitch .foot-state').textContent==='ON');
 await loop.click(); await page.waitForFunction(()=>document.querySelectorAll('.footswitch')[1].getAttribute('aria-pressed')==='true');
 await loop.hover(); await page.mouse.down(); await page.waitForTimeout(700);
 await page.waitForFunction(()=>document.querySelectorAll('.footswitch .foot-state')[1].textContent==='OVERDUB');
 await page.mouse.up(); await page.waitForFunction(()=>document.querySelectorAll('.footswitch .foot-state')[1].textContent==='PLAYING');
 console.log('wet freeze and loop overdub gestures passed');
 await page.locator('input[type=file]').setInputFiles(stereoPath);
 await page.getByText('File loaded and looping.').waitFor();
 assert(await page.getByText('brood-stereo-fixture.wav').isVisible());
 await page.getByRole('button',{name:'LIVE IN'}).click();
 await page.getByText('Your microphone or audio interface. Use headphones.').waitFor();
 await page.waitForFunction(()=>parseFloat(document.querySelector('.meter i').style.width)>1, null, {timeout:10000});
 const meter = await page.locator('.meter i').first().evaluate(el=>parseFloat(el.style.width));
 assert(meter > 1, 'fake microphone has nonzero input meter: '+meter);
 await page.getByRole('button',{name:'DEMO', exact:true}).click();
 await page.getByRole('button',{name:'PAUSE AUDIO'}).click();
 await page.getByRole('button',{name:'START AUDIO'}).click();
 await page.getByRole('button',{name:'PAUSE AUDIO'}).waitFor();
 await page.waitForFunction(()=>document.querySelectorAll('.footswitch')[1].getAttribute('aria-pressed')==='true');
 console.log('file, fake mic, pause/resume passed (meter '+meter+'%)');
 await page.getByLabel('Choose patch').selectOption('2');
 assert.equal(await page.getByRole('radio',{name:'DELAY',exact:true}).getAttribute('aria-checked'), 'true');
 const time = page.getByRole('slider',{name:'TIME',exact:true});
 await time.focus(); const before=+await time.getAttribute('aria-valuenow'); await page.keyboard.press('ArrowUp');
 assert.equal(+await time.getAttribute('aria-valuenow'), before+1);
 const box = await time.boundingBox(); await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.move(box.x+box.width/2,box.y+box.height/2-25);await page.mouse.up();
 assert(+await time.getAttribute('aria-valuenow')>before+1);
 const saved=await time.getAttribute('aria-valuenow');
 await page.getByRole('button',{name:'[ SAVE PATCH ]'}).click();
 await page.reload();
 assert.equal(await time.getAttribute('aria-valuenow'),saved);
 assert(await page.locator('option').filter({hasText:'YOUR PATCH'}).count());
 console.log('presets, save/reload, keyboard and drag passed');
 await page.setViewportSize({width:390,height:844});
 await page.getByRole('button',{name:/HOW TO PLAY/}).click();
 assert(await page.getByRole('region',{name:'Field guide'}).isVisible());
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),390);
 await page.setViewportSize({width:320,height:844});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),320);
 if (process.env.BROOD_TEST_SCREENSHOT) await page.screenshot({path:process.env.BROOD_TEST_SCREENSHOT, fullPage:true});
 assert.deepEqual(errors, []);
 console.log('mobile guide and overflow passed; no browser errors');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => { if (browser) await browser.close(); fs.rmSync(temp, { recursive:true, force:true }); });
