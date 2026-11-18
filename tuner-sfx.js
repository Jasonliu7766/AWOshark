// tuner-sfx.js — 给 tuner.html 加一个"声音调试"面板（浏览器 Web Audio 预览，算法与固件一致）
// 用法：把本文件放到 tuner.html 同目录，并在 tuner.html 的 </body> 前加一行：
//   <script src="tuner-sfx.js"></script>
// 或直接把本文件内容用 <script> ... </script> 包起来粘到 tuner.html 的 </body> 前。
(function () {
  const RATE = 16000;
  // 效果定义（和 mp_sound_test.py v2 的段落一致）；段可为 {silence:ms}
  const SEG_ANGER = {f0:440,f1:440,ms:130,parts:[[1,1],[1.414,.9]],growlHz:20,growlD:.5,drive:2,amp:.85,atk:.005,rel:.1};
  const EFFECTS = {
    '惨叫': [
      {f0:1500,f1:1300,ms:90, parts:[[1,1],[2.01,.5]],            noise:.25,drive:2.2,amp:.9, vibHz:16,vibD:.08,atk:.005,rel:.1},
      {f0:1250,f1:520, ms:430,parts:[[1,1],[1.01,.7],[.5,.4]],    noise:.30,drive:2.6,amp:1,  vibHz:13,vibD:.10,pcurve:1.6,atk:.004,rel:.25},
    ],
    '吼叫': [
      {f0:150,f1:60,ms:900,parts:[[1,1],[.5,.8],[2,.3]],growlHz:48,growlD:.7,noise:.25,drive:2.8,amp:1,vibHz:5,vibD:.06,pcurve:1.3,atk:.01,rel:.35},
    ],
    '怒气警告': [SEG_ANGER, {silence:45}, SEG_ANGER, {silence:45}, SEG_ANGER],
    '开机音': [
      {f0:70,f1:180,ms:700,parts:[[1,1],[1.5,.5],[2,.3]],growlHz:12,growlD:.3,drive:1.6,amp:.7,atk:.08,rel:.3},
    ],
    '摸头呼噜': [
      {f0:85,f1:100,ms:700,parts:[[1,1],[2,.4]],growlHz:26,growlD:.6,noise:.1,drive:1.4,amp:.55,atk:.05,rel:.3},
    ],
    '拍手回应': [
      {f0:300,f1:700,ms:160,parts:[[1,1],[1.5,.4]],growlHz:30,growlD:.4,drive:1.8,amp:.6,vibHz:10,vibD:.05,atk:.01,rel:.2},
    ],
  };
  // 每个效果的可调旋钮（乘在定义上）
  const KNOBS = {};
  for (const k in EFFECTS) KNOBS[k] = {pitch:1, len:1, drive:1, growl:1, noise:1, vib:1, vol:1};
  let gVol = 0.85, cur = '惨叫';
  let actx = null;
  const ac = () => (actx = actx || new (window.AudioContext || window.webkitAudioContext)());

  function renderSeg(seg, k) {
    const ms = seg.ms * k.len, n = Math.max(1, Math.round(RATE * ms / 1000));
    const out = new Float32Array(n), parts = seg.parts || [[1, 1]];
    const ph = parts.map(() => 0); let gph = 0, nz = 0;
    const drive = (seg.drive || 1) * k.drive, growlD = (seg.growlD || 0) * k.growl;
    const noise = (seg.noise || 0) * k.noise, vibD = (seg.vibD || 0) * k.vib;
    const amp = (seg.amp || .6) * k.vol * gVol, atk = seg.atk || .02, rel = seg.rel || .3, pc = seg.pcurve || 1;
    for (let i = 0; i < n; i++) {
      const t = i / n;
      let f = (seg.f0 + (seg.f1 - seg.f0) * Math.pow(t, pc)) * k.pitch;
      if (vibD) f *= 1 + vibD * Math.sin(2 * Math.PI * (seg.vibHz || 0) * i / RATE);
      let s = 0;
      for (let p = 0; p < parts.length; p++) { ph[p] += f * parts[p][0] / RATE; s += parts[p][1] * Math.sin(2 * Math.PI * ph[p]); }
      if (growlD) { gph += (seg.growlHz || 0) / RATE; s *= (1 - growlD) + growlD * (0.6 + 0.4 * Math.sin(2 * Math.PI * gph)) * 2; }
      if (noise) { nz += 0.3 * ((Math.random() * 2 - 1) - nz); s = s * (1 - noise) + nz * noise * 3; }
      s *= drive; s = s > 1 ? 1 : (s < -1 ? -1 : s);
      const env = t < atk ? t / atk : (t > 1 - rel ? (1 - t) / rel : 1);
      out[i] = s * amp * env;
    }
    return out;
  }
  // SFX_UI_PLACEHOLDER
  function renderEffect(name) {
    const k = KNOBS[name], chunks = []; let total = 0;
    for (const seg of EFFECTS[name]) {
      const c = seg.silence != null ? new Float32Array(Math.round(RATE * seg.silence / 1000)) : renderSeg(seg, k);
      chunks.push(c); total += c.length;
    }
    const out = new Float32Array(total); let o = 0;
    for (const c of chunks) { out.set(c, o); o += c.length; }
    return out;
  }
  function play(name) {
    const a = ac(); if (a.state === 'suspended') a.resume();
    const data = renderEffect(name);
    const b = a.createBuffer(1, data.length, RATE); b.getChannelData(0).set(data);
    const src = a.createBufferSource(); src.buffer = b; src.connect(a.destination); src.start();
    return data.length / RATE;
  }
  async function playAll() { for (const n of Object.keys(EFFECTS)) { const d = play(n); await new Promise(r => setTimeout(r, d * 1000 + 350)); } }

  const KDEF = [['pitch','音调×',.5,2,.05],['len','时长×',.5,2,.05],['drive','失真',.3,2.5,.05],
                ['growl','喉音',0,2,.05],['noise','嘶哑',0,2,.05],['vib','颤音',0,2,.05]];
  function buildKnobs(host) {
    const k = KNOBS[cur];
    host.innerHTML = '';
    for (const [key, label, mn, mx, st] of KDEF) {
      const d = document.createElement('div'); d.className = 'param';
      d.innerHTML = `<label>${label} <small id="sfxv_${key}">${k[key]}</small></label>
        <input type="number" min="${mn}" max="${mx}" step="${st}" value="${k[key]}" style="width:72px">
        <input type="range" min="${mn}" max="${mx}" step="${st}" value="${k[key]}">`;
      const [num, rng] = d.querySelectorAll('input');
      const set = v => { KNOBS[cur][key] = +v; num.value = rng.value = v; document.getElementById('sfxv_' + key).textContent = v; };
      rng.addEventListener('input', () => set(rng.value));
      num.addEventListener('change', () => set(num.value));
      host.appendChild(d);
    }
  }
  function init() {
    const main = document.querySelector('main') || document.body;
    const card = document.createElement('section'); card.className = 'card wide';
    const btns = Object.keys(EFFECTS).map(n => `<button data-fx="${n}">${n}</button>`).join('');
    card.innerHTML = `<h2>🔊 声音调试（浏览器预览）<button id="sfxAll">依次播放全部</button></h2>
      <p class="hint">浏览器里合成预览，算法和固件一致。拖旋钮调"恐怖/真实"程度，满意后点"导出参数"发我，我烧进小鲨。</p>
      <div class="row" id="sfxBtns">${btns}</div>
      <div class="row"><b>当前：<span id="sfxCur">${cur}</span></b></div>
      <div id="sfxKnobs"></div>
      <div class="row"><button class="primary" id="sfxPlay">▶ 试听当前</button>
        <span class="hint">音量</span><input type="range" id="sfxVol" min="0" max="100" value="85" style="flex:1;min-width:120px"><b id="sfxVolV">85</b>
        <button id="sfxExport">导出参数</button></div>
      <pre id="sfxOut" style="height:120px;overflow:auto;background:var(--bg);border-radius:8px;padding:8px;font:12px/1.4 ui-monospace,Menlo,monospace;white-space:pre-wrap;margin:6px 0 0"></pre>`;
    main.appendChild(card);
    const knobHost = card.querySelector('#sfxKnobs');
    buildKnobs(knobHost);
    card.querySelector('#sfxBtns').addEventListener('click', e => {
      const b = e.target.closest('button'); if (!b || !b.dataset.fx) return;
      cur = b.dataset.fx; card.querySelector('#sfxCur').textContent = cur; buildKnobs(knobHost); play(cur);
    });
    card.querySelector('#sfxPlay').onclick = () => play(cur);
    card.querySelector('#sfxAll').onclick = playAll;
    const vol = card.querySelector('#sfxVol');
    vol.oninput = () => { gVol = vol.value / 100; card.querySelector('#sfxVolV').textContent = vol.value; };
    card.querySelector('#sfxExport').onclick = () => {
      card.querySelector('#sfxOut').textContent = JSON.stringify(KNOBS, null, 1);
    };
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();

})();
