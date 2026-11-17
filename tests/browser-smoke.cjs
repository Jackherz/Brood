const assert = require('node:assert/strict');
// Optional end-to-end smoke test. Start Vite first and set CHROMIUM_PATH.
// Any Chromium browser will do, Brave included:
//   CHROMIUM_PATH="/Applications/Brave Browser.app/Contents/MacOS/Brave Browser" npm run test:browser
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
     await ctx.audioWorklet.addModule('/src/brood-worklet.js');
     const node = new AudioWorkletNode(ctx, 'brood-processor', { numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[2], channelCount:2, channelCountMode:'explicit' });
     let wetSeen = false;
     node.port.onmessage = e => { if (e.data.type === 'state' && e.data.st.wetFed) wetSeen = true; };
     const b = ctx.createBuffer(2, fs*2, fs), left=b.getChannelData(0);
     for(let i=0;i<left.length;i++) left[i]=0.3*Math.sin(2*Math.PI*440*i/fs);
     const s=ctx.createBufferSource();s.buffer=b;s.connect(node);node.connect(ctx.destination);s.start();
     // Wait for the worklet to exist, then send messages while offline rendering
     // is suspended. Sending before startRendering can race a fast renderer.
     const paused = ctx.suspend(0.05), rendering = ctx.startRendering();
     await paused;
     if (wet) {
       // WET is fed whenever ROUTING isn't OFF; WET MIX at the top hears it alone.
       for (const [name,value] of Object.entries({wetMode:1, time:0.5, wetMix:1, clock:1, modWet:0.4})) node.port.postMessage({type:'param', name, value});
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
 const lcd = () => page.locator('.lcd').textContent();
 // The screen shows help while the mouse rests on a control, so move it off the panel first.
 const readout = async (pattern) => { await page.mouse.move(2, 2); await page.waitForFunction((src) => new RegExp(src).test(document.querySelector('.lcd').textContent), pattern.source) };
 // A first visit opens the tour: each step opens more controls, what it opens stays open, and the rest is veiled.
 const veiled = (name) => page.getByRole('slider',{name,exact:true}).evaluate((e) => !!e.closest('[inert]'));
 const tourTitle = () => page.locator('.tourbar h2').textContent();
 assert.equal(await tourTitle(), 'START WITH SOUND');
 assert.equal(await veiled('TIME'), true);
 // Doing a step's task lights NEXT up; the tour never moves on by itself.
 assert.equal(await page.locator('.tour-ring').count(), 1);
 await page.getByRole('button',{name:'START AUDIO'}).click();
 await page.waitForSelector('.tour-ready', {timeout: 6000});
 assert.equal(await page.locator('.tour-ring').count(), 0);
 assert.equal(await tourTitle(), 'START WITH SOUND');
 await page.getByRole('button',{name:'NEXT',exact:true}).click();
 assert.equal(await tourTitle(), 'CATCH A LOOP');
 assert.equal(await veiled('LENGTH'), false);
 assert.equal(await veiled('TIME'), true);
 await page.getByRole('button',{name:'Step 5: add some space'}).click();
 assert.equal(await veiled('TIME'), false);
 assert.equal(await veiled('LENGTH'), false);   // opened in step 2, and still open
 assert.equal(await page.getByRole('button',{name:/^Reset/}).evaluate((e) => !!e.closest('[inert]')), true);   // only ALL opens the header
 await page.getByRole('button',{name:/^All of Brood/}).click();
 assert.equal(await page.locator('.stage [inert]').count(), 0);
 await page.getByRole('button',{name:'END TOUR',exact:true}).click();
 await page.reload();
 assert.equal(await page.locator('.tourbar').count(), 0);
 console.log('tour steps, veil, jump and end passed');
 await page.getByRole('button',{name:'START AUDIO'}).click();
 await page.getByRole('button',{name:'STOP AUDIO'}).waitFor();
 const wet = page.getByRole('button',{name:/^FREEZE: tap or hold/});
 const loop = page.getByRole('button',{name:/^LOOP: tap or hold/});
 const frozen = (on) => page.waitForFunction((v)=>document.querySelector('.footswitch').getAttribute('aria-pressed')===v, on ? 'true' : 'false');
 // A tap latches FREEZE, another lets it go; held, it freezes only while held.
 await wet.click(); await frozen(true);
 await wet.click(); await frozen(false);
 await wet.hover(); await page.mouse.down(); await page.waitForTimeout(700);
 await frozen(true);
 await page.mouse.up(); await frozen(false);
 await page.waitForTimeout(1500);   // a little history for LOOP to catch
 await loop.click(); await readout(/PLAYING/);
 await loop.hover(); await page.mouse.down(); await page.waitForTimeout(700);
 await readout(/OVERDUBBING/);
 await page.mouse.up(); await readout(/PLAYING/);
 console.log('freeze key and loop overdub gestures passed');
 // UNDO counts the take and the layer; RESAMPLE adds a third; UNDO goes back and REDO can return.
 const undoKey = () => page.evaluate(()=>[...document.querySelectorAll('.strip-key')].map((b)=>b.textContent + (b.disabled ? '-' : '')).join(' '));
 await page.waitForFunction(()=>[...document.querySelectorAll('.strip-key')].some((b)=>b.textContent==='UNDO 2'));
 await page.getByRole('switch',{name:'RESAMPLE',exact:true}).click();
 await page.waitForFunction(()=>[...document.querySelectorAll('.strip-key')].some((b)=>b.textContent==='UNDO 3'), null, {timeout: 40000});
 await page.getByRole('button',{name:'UNDO 3',exact:true}).click();
 await page.waitForFunction(()=>[...document.querySelectorAll('.strip-key')].some((b)=>b.textContent==='UNDO 2'));
 assert(/REDO(?!-)/.test(await undoKey()), await undoKey());
 console.log('undo, resample and redo passed');
 await page.locator('input[type=file]').setInputFiles(stereoPath);
 await readout(/LOADED, AND LOOPING INTO BROOD/);
 assert(await page.getByText('BROOD-STEREO-FIXTURE.WAV', { exact: true }).isVisible());
 await page.getByRole('radio',{name:'MIC',exact:true}).click();
 await readout(/THE MICROPHONE/);
 await page.getByRole('radio',{name:'DEMO',exact:true}).click();
 await page.getByRole('button',{name:'STOP AUDIO'}).click();
 await page.getByRole('button',{name:'START AUDIO'}).click();
 await page.getByRole('button',{name:'STOP AUDIO'}).waitFor();
 console.log('file, fake mic, stop and start passed');
 await page.getByRole('button',{name:/Browse presets/}).click();
 await page.getByRole('button',{name:'GLASS ECHO',exact:true}).click();
 await page.getByRole('button',{name:'KEEP',exact:true}).click();
 assert.equal(await page.getByRole('radio',{name:'DELAY',exact:true}).getAttribute('aria-checked'), 'true');
 const time = page.getByRole('slider',{name:'TIME',exact:true});
 await time.focus(); const before=+await time.getAttribute('aria-valuenow'); await page.keyboard.press('ArrowUp');
 assert.equal(+await time.getAttribute('aria-valuenow'), before+5);
 const box = await time.boundingBox(); await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.move(box.x+box.width/2,box.y+box.height/2-25,{steps:3});await page.mouse.up();
 assert(+await time.getAttribute('aria-valuenow')>before+5);
 const saved=await time.getAttribute('aria-valuenow');
 page.once('dialog', d => d.accept('my sound'));
 await page.getByRole('button',{name:'SAVE',exact:true}).click();
 await readout(/SAVED MY SOUND ON THIS DEVICE/);
 // The take's END marker: press near the right edge of a playing loop and drag it in, and END
 // stops being the whole take; a double-click opens it again.
 await loop.click(); await loop.click();
 await page.waitForFunction(()=>/^The take\. START/.test(document.querySelector('.take').getAttribute('aria-label')));
 const waveBox = await page.locator('.take canvas').boundingBox();
 await page.mouse.move(waveBox.x+waveBox.width-3, waveBox.y+waveBox.height/2); await page.mouse.down();
 await page.mouse.move(waveBox.x+waveBox.width*0.6, waveBox.y+waveBox.height/2, {steps:5}); await page.mouse.up();
 await page.waitForFunction(()=>/END plays to/.test(document.querySelector('.take').getAttribute('aria-label')));
 await page.locator('.take canvas').dblclick();
 await page.waitForFunction(()=>/END to the end/.test(document.querySelector('.take').getAttribute('aria-label')));
 console.log('take END marker drag and reset passed');
 // Modulation: drag MOD 1 onto TIME, and a ring and a route appear.
 const chip = await page.getByRole('button',{name:/^MOD 1,/}).boundingBox();
 const timeBox = await time.boundingBox();
 await page.mouse.move(chip.x+chip.width/2,chip.y+chip.height*0.7); await page.mouse.down();
 await page.mouse.move(timeBox.x+timeBox.width/2,timeBox.y+timeBox.height/2,{steps:6}); await page.mouse.up();
 assert.equal(await time.locator('path[stroke="#b4452a"]').count(),1);
 await page.getByRole('button',{name:'ROUTES',exact:true}).click();
 assert(await page.getByRole('slider',{name:'MOD 1 depth on TIME'}).isVisible());
 await page.getByRole('button',{name:'Remove MOD 1 from TIME'}).click();
 await page.getByRole('button',{name:'CLOSE',exact:true}).click();
 console.log('modulation route by drag passed');
 await page.reload();
 assert.equal(await time.getAttribute('aria-valuenow'),saved);
 await page.getByRole('button',{name:/Browse presets/}).click();
 assert(await page.getByRole('button',{name:/^(> )?MY SOUND$/}).isVisible());
 await page.getByRole('button',{name:'CANCEL',exact:true}).click();
 console.log('presets, save/reload, keyboard and drag passed');
 // A phone gets the phone layout: one column's knobs at a time, and never a sideways scroll.
 await page.setViewportSize({width:390,height:844});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),390);
 await page.getByRole('slider',{name:'TIME',exact:true}).waitFor({timeout:5000});
 await page.getByRole('radio',{name:'MICRO-LOOPER',exact:true}).click();
 await page.getByRole('slider',{name:'LENGTH',exact:true}).waitFor({timeout:5000});
 assert.equal(await page.getByRole('slider',{name:'TIME',exact:true}).count(), 0);
 await page.setViewportSize({width:320,height:640});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),320);
 if (process.env.BROOD_TEST_SCREENSHOT) await page.screenshot({path:process.env.BROOD_TEST_SCREENSHOT, fullPage:true});
 assert.deepEqual(errors, []);
 console.log('phone layout and no browser errors passed');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => { if (browser) await browser.close(); fs.rmSync(temp, { recursive:true, force:true }); });
