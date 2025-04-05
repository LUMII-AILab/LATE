
import { editorSchema } from './schema.js';

import { msToTime, copyTextToClipboard, saveAs, toSRTtime, extractAudio, AudioRecorder, AudioRecorderVAD, generateRandomId, concatenateAudioBlobs, concatenateAudioBuffers, decodeAudioData, buffer2wav } from './utils.js';

import { setupEditor } from './editor.js';

import { setupWaveform } from './waveform.js';

import { select, enable, disable, hide, unhide, on, onclick, oninput, onchange, addclass, rmclass, hasclass } from './dom.js';



// Polyfill for non-HTTPS connections
// Check if crypto.randomUUID is available
if (typeof crypto === 'undefined' || typeof crypto.randomUUID === 'undefined') {
  // Polyfill for crypto.randomUUID
  crypto.randomUUID = function() {
    // Helper function to generate a random 16-bit number
    function getRandom16Bit() {
      return Math.floor((1 + Math.random()) * 0x10000).toString(16).substring(1);
    }

    // Generate a UUID in the format xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    // where x is any hexadecimal digit and y is one of 8, 9, A, or B
    const uuid = (
      getRandom16Bit() + getRandom16Bit() + '-' +
      getRandom16Bit() + '-' +
      '4' + getRandom16Bit().substr(0, 3) + '-' +
      ((Math.floor(Math.random() * 4) + 8).toString(16)) + getRandom16Bit().substr(0, 3) + '-' +
      getRandom16Bit() + getRandom16Bit() + getRandom16Bit()
    );

    return uuid;
  };
}



function nodesToSRT(doc) {
  // {
  //   "type": "doc",
  //   "content": [
  //     {
  //       "type": "paragraph",
  //       "content": [
  //         {
  //           "type": "span",
  //           "attrs": {
  //             "start": 8,
  //             "end": 23,
  //             "highlighted": false
  //           },
  //           "content": [
  //             {
  //               "type": "text",
  //               "text": " Nos"
  //             }
  //           ]
  //         },

  let subtitles = [];

  for (const paragraph of doc.content) {
    let text = '';
    let start = 0;
    let end = 0;
    for(const span of paragraph.content || []) {
      if (span.type !== 'span')
        continue;
      if(start === 0) {
        start = span.attrs.start;
      }
      text += span.content.map((item) => item.text).join('');
      end = span.attrs.end;
    }

    if (text.length > 0) {
      subtitles.push(`${subtitles.length + 1}\n${toSRTtime(start)} --> ${toSRTtime(end)}\n${text.trim()}`);
    }
  }

  return subtitles.join('\n\n');
}

function whisperSegmentToParagraphNode(segment, cutoff) {
  const paragraph = { type: 'paragraph', content: [], attrs: {} };

  // remove space at the beginning of new paragraph
  if (segment.tokens.length > 0 && !segment.tokens[0].special) {
    segment.tokens[0].text = segment.tokens[0].text.trimStart();
  } else if (segment.tokens.length > 1 && !segment.tokens[1].special) {
    segment.tokens[1].text = segment.tokens[1].text.trimStart();
  }

  for (const token of segment.tokens) {
    if (token.special || token.text.length === 0) {
      continue;
    }
    if (cutoff !== undefined && cutoff > 0 && token.start > cutoff) {
      continue;
    }
    paragraph.attrs.start = msToTime(segment.start * 10, { hours: false });
    paragraph.attrs.end = msToTime(segment.end * 10, { hours: false });
    paragraph.content.push({
      type: 'span',
      attrs: {
        start: token.start,
        end: token.end,
        p: token.p,
      },
      content: [{
        type: 'text',
        text: token.text.replaceAll('ó', '\u{1F3B6}'),
      }]
    });
  }

  return paragraph;
}

function whisperToNodes(input, cutoff) {
  // input format:
  // {
  //   "lang": "lv",
  //   "segments": [
  //     {
  //       "speaker_turn": false,
  //       "text": " Noslēgusies 5,9 miljonu eiro vērtā silenas un pārtarnieku robežu punktu modernizācija.",
  //       "tokens": [
  //         {
  //           "dtw": -1,
  //           "end": 0,
  //           "p": 0.5043591856956482,
  //           "plog": -0.6844666004180908,
  //           "pt": 0.5043591856956482,
  //           "ptsum": 0.971691370010376,
  //           "special": true,
  //           "start": 0,
  //           "text": "[_BEG_]",
  //           "vlen": 7.0
  //         },
  //

  // output format:
  // {
  //   "type": "doc",
  //   "content": [
  //     {
  //       "type": "paragraph",
  //       "content": [
  //         {
  //           "type": "span",
  //           "attrs": {
  //             "text": "Hello,",
  //             "start": "0",
  //             "end": "1"
  //           }
  //         },

  const output = { type: 'doc', content: [] };

  for (const segment of input.segments) {

    const paragraph = whisperSegmentToParagraphNode(segment, cutoff);

    output.content.push(paragraph);
  }

  return output;
}

function augmentDocJSON(doc) {
  for (const paragraph of doc.content) {
    if (paragraph.type === 'paragraph') {
      if ((paragraph.attrs === undefined || (paragraph.attrs.start === undefined && paragraph.attrs.end === undefined)) && paragraph.content && paragraph.content.length > 0) {
        let firstSpan, lastSpan;
        for (let i = 0; i < paragraph.content.length; i++) {
          const node = paragraph.content[i];
          if (node.type === 'span') {
            firstSpan = node;
            break;
          }
        }
        for (let i = paragraph.content.length - 1; i >= 0; i--) {
          const node = paragraph.content[i];
          if (node.type === 'span') {
            lastSpan = node;
            break;
          }
        }
        if (paragraph.attrs === undefined) {
          paragraph.attrs = {};
        }
        if (firstSpan) {
          paragraph.attrs.start = msToTime(firstSpan.attrs.start * 10, { hours: false });
        }
        if (lastSpan) {
          paragraph.attrs.end = msToTime(lastSpan.attrs.end * 10, { hours: false });
        }
      }
    }
  }
  return doc;
}


async function main() {

  const dom = {
    zoomSlider: select('#zoom'),
    zoomInButton: select('#zoom-in'),
    zoomOutButton: select('#zoom-out'),
    waveform: select('#waveform'),
    waveformPlaceholder: select('#waveform-placeholder'),
    editorContainer: select('#content'),
    playbackRateSlider: select('#playback-rate'),
    playbackRateDisplay: select('#playback-rate-display'),
    timestamp: select('#timestamp'),
    playPauseButton: select('button.play-pause'),
    fileSelector: select('#file-selector'),
    topSpinner: select('#top-spinner'),
    recordingTime: select('#record-time'),
    recordButton: select('#record'),
    rootContainer: select('#root'),
    menu: select('#menu'),
    overlay: select('#overlay'),
    abortButton: select('#abort'),
    saveButton: select('#save'),
    shareButton: select('#share'),
    abortButton: select('#abort'),
    copyLink: select('#copy-link'),
    removeDocument: select('#remove-document'),
  };

  const waveform = await setupWaveform({ container: '#waveform' });

  function playPause() {
    console.log('playpause get duration:', waveform.getDuration())
    if (waveform.getDuration() > 0) {
      console.log('playpause playpause')
      waveform.playPause();
    } else {
      console.log('playpause pause')
      waveform.pause();
    }
  }

  const editor = await setupEditor({ playPause, onDocumentChanged, container: '#content' });

  dom.editor = editor.dom;

  let documentChanged = false;
  let documentOwner = false;
  let processingID;


  editor.dom.setAttribute('spellcheck', 'false');


  // setup waveform zoom controls

  const zoomSliderMin = parseInt(dom.zoomSlider.min);
  const zoomSliderMax = parseInt(dom.zoomSlider.max);

  function updateZoomButtonState() {
    let value = dom.zoomSlider.valueAsNumber;
    if (value <= zoomSliderMin) {
      disable(dom.zoomOutButton);
    } else {
      enable(dom.zoomOutButton);
    }
    if (value >= zoomSliderMax) {
      disable(dom.zoomInButton);
    } else {
      enable(dom.zoomInButton);
    }
  }

  updateZoomButtonState();

  oninput(dom.zoomSlider, (event) => {
    const minPxPerSec = event.target.valueAsNumber;
    waveform.zoom(minPxPerSec);
  });
  onclick(dom.zoomInButton, (event) => {
    dom.zoomSlider.value = dom.zoomSlider.valueAsNumber + 10;
    const minPxPerSec = dom.zoomSlider.valueAsNumber;
    waveform.zoom(minPxPerSec);
    updateZoomButtonState();
  });
  onclick(dom.zoomOutButton, (event) => {
    dom.zoomSlider.value = dom.zoomSlider.valueAsNumber - 10;
    const minPxPerSec = dom.zoomSlider.valueAsNumber;
    waveform.zoom(minPxPerSec);
    updateZoomButtonState();
  });

  // setup waveform playback rate controls

  function setPlaybackRate(value, preservePitch = true) {
    waveform.setPlaybackRate(value, preservePitch);
    dom.playbackRateDisplay.innerText = `${value}x`;
    if (dom.playbackRateSlider.valueAsNumber * 0.25 != value) {
      dom.playbackRateSlider.value = value / 0.25;
    }
  }

  oninput(dom.playbackRateSlider, (event) => {
    const value = event.target.valueAsNumber * 0.25;
    setPlaybackRate(value);
  });

  setPlaybackRate(1);

  // setup waveform event handlers

  function timeUpdated(currentTime) {
    editor.setTime(currentTime*100);
    const duration = waveform.getDuration();
    dom.timestamp.textContent = msToTime(currentTime*1000, {ms:false}) + ' / ' + msToTime(duration*1000, {ms:false});
  }

  waveform.on('timeupdate', (currentTime) => {
    window.requestAnimationFrame(() => timeUpdated(currentTime));
  })
  waveform.on('play', () => {
    dom.editor.classList.add('playing');
    dom.playPauseButton.classList.remove('play');
  });
  waveform.on('pause', () => {
    dom.editor.classList.remove('playing');
    dom.playPauseButton.classList.add('play');
  });
  waveform.on('load', () => {
    waveform.setTime(0);
    dom.waveform.classList.add('loading');
    hide(dom.waveformPlaceholder);
  });
  waveform.on('ready', () => {
    dom.waveform.classList.remove('loading');
    waveform.setTime(0);
    if (waveform.getDuration() > 0) {
      enable(dom.playPauseButton);
    } else {
      disable(dom.playPauseButton);
      unhide(dom.waveformPlaceholder);
    }
  });



  timeUpdated(editor.getTime()/100); // TODO: need consistency between seconds and miliseconds

  onclick(dom.playPauseButton, (event) => {
    console.log('play clicked');
    playPause();
  });

  onclick('#copy-text', () => {
    copyTextToClipboard(editor.getText());
  });

  function toggleDropdown(button) {
    (button instanceof Event ? button.target : button).parentNode.querySelector('.dropdown-content').classList.toggle('show');
  }

  // Close the dropdown if the user clicks outside of it
  window.onclick = function(event) {
    if (!event.target.matches('.dropdown-btn')) {
      const dropdowns = document.querySelectorAll(".dropdown-content");
      for (const dropdown of dropdowns) {
        if (dropdown.classList.contains('show')) {
          dropdown.classList.remove('show');
        }
      }
    }
  }

  onclick('#save-srt', toggleDropdown);

  onclick('#save-subtitle', () => {
    const srt = nodesToSRT(editor.toJSON())

    if (srt.length === 0) {
      return;
    }

    const filename = 'output.srt';

    let blob = new Blob([srt], { type: 'application/x-subrip' }, filename);

    saveAs(blob, filename);
  });

  onclick('#save-audio', () => {
    // const url = document.querySelector('#waveform > div:nth-child(2)').shadowRoot.querySelector('audio').src;
    const url = dom.waveform.children[1].shadowRoot.querySelector('audio').src;
    if (url && url.length > 0) {
      saveAs(url, 'audio.wav');
    }
  });

  let startNewDocument = false;

  async function appendBlob(audioBlob) {
    // TODO:
    // 1. load
    // 2. append to waveform
    // 3. transcribe
    // 4. append to transcription
  }

  onclick('#upload', (event) => {
    // check if file is already loaded
    // if(file !== undefined) {
    //   window.open(location.href, '_blank');
    //   return;
    // }

    startNewDocument = true;
    dom.fileSelector.click();
  });

  // load configuration
 
  const config = { whisper: {} };

  {
    const response = await fetch(`./api/config`, { method: 'GET', headers: { 'Accept': 'application/json' } });
    const c = await response.json();
    if (c.whisper !== undefined && c.whisper.limit > 0) {
      config.whisper.limit = c.whisper.limit;
    }
  }

  onchange(dom.fileSelector, async (event) => {
    const newDocument = startNewDocument;
    const file = event.target.files[0];
    // reset file selector: https://stackoverflow.com/a/35323290
    event.target.value = ''
    if(!/safari/i.test(navigator.userAgent)){
      event.target.type = ''
      event.target.type = 'file'
    }
    if(!file) {
      return;
    }

    if (newDocument) {
      documentID = '';
      location.hash = '';
    }

    // start spinner
    // setState('busy');

    unhide(dom.topSpinner);
    addclass(dom.waveform, 'loading');
    hide(dom.waveformPlaceholder);

    let audio;
    try {
      audio = await extractAudio(file, 16000, 1, documentID ? undefined : config.whisper.limit /* limit duration to N seconds */);
    } catch(e) {
      console.log(e);
      setState('error', 'failed to extract audio');
      rmclass(dom.waveform, 'loading');
      hide(dom.topSpinner);
      return;
    }

    // ---
    if (waveform.getDuration() > 0) {
      // append
      console.log('audio', audio)
      const currentAudioBuffer = waveform.getDecodedData();
      console.log('current', currentAudioBuffer)
      const audioBuffer = await decodeAudioData(audio);
      console.log('audio buffer', audioBuffer)
      const newAudioBuffer = concatenateAudioBuffers(new AudioContext(), currentAudioBuffer, audioBuffer);
      audio = buffer2wav(newAudioBuffer)
    }

    try {
      await waveform.loadBlob(audio);
      rmclass(dom.waveform, 'loading');
      setTimeout(() => {
        waveform.zoom(dom.zoomSlider.valueAsNumber);
        setPlaybackRate(1);
      }, 300);
    } catch(e) {
      console.log(e);
      setState('error', 'loading audio failed');
      rmclass(dom.waveform, 'loading');
      hide(dom.topSpinner);
      return;
    }

    // return;

    if (documentID) {
      hide(dom.topSpinner);
      setState();
      return;
    }

    editor.clear();

    let result;

    try {
      // const language = 'auto';
      const language = 'lv';
      const formData = new FormData();
      formData.append('input', audio, 'dummy');
      formData.append('lang', language);

      // const response = await fetch(`./api/whisper`, { method: 'POST', body: formData, headers: { 'Accept': 'application/json' } });
      const response = await fetch(`./api/whisper?q=t`, { method: 'POST', body: formData, headers: { 'Accept': 'application/json' } });

      if (!response.ok) {
        if (response.status == 413) {
          setState('error', 'payload too large');
        }
        setState('error');
        hide(dom.topSpinner);
        return;
      }

      result = await response.json();

      hide(dom.topSpinner);

      location.hash = result.id;

      processingID = result.id;

      collectResults(result.id);
      return;

    } catch(e) {
      console.log(e);
      setState('error', `upload failed`);
      // setState('error', `upload failed: ${e.message}`);
      return;
    }

    try {
      const docJSON = whisperToNodes(result /*, cutoff - expose from inside extractAudio */);
      editor.fromJSON(docJSON);
    } catch(e) {
      console.log(e);
      setState('error', e.message);
      return;
    }

    // stop spinner
    setState();
  });

  // https://stackoverflow.com/questions/31061838/how-do-i-cancel-an-http-fetch-request
  let abortController;

  // // this is how to abort
  // onclick('button#cancel', () => { abortController.abort(); });

  async function collectResults(id) {

    addclass(dom.editor, 'processing');

    {
      const response = await fetch(`./api/whisper/${id}/status`);
      const status = await response.text();
      console.log('job status:', status);
      if (status === 'waiting' || status === 'running') {
        unhide(dom.abortButton);
      }
    }

    abortController = new AbortController();
    const response = await fetch(`./api/whisper/${id}/wait`, { method: 'GET',
      headers: { 'Accept': 'application/jsonl; application/jsonl; application/x-ndjson; */*' }, signal: abortController.signal });
    if(!response.ok) {
      rmclass(dom.editor, 'processing');
      // // spinner.style.display = 'none';
      // hideLoading();
      return;
    }

    // if (!response.ok) {
    //   if (response.status == 413) {
    //     setState('error', 'payload too large');
    //   }
    //   setState('error');
    //   return;
    // }

    const processLine = (line) => {
      if (!line)
        return;
      line = line.trim();
      if (line.length == 0)
        return;

      // console.log('got result line:', line);
      const segment = JSON.parse(line);

      if(segment.done) {
        console.log('done');
        return true;
      }

      if(segment.error !== undefined || segment.text === undefined) {
        // TODO: stop "processing"
        console.log('got error:', segment.error);
        return false;
      }

      console.log('got result segment:', segment);

      const paragraphJSON = whisperSegmentToParagraphNode(segment);

      console.log('paragraph node JSON:', paragraphJSON);

      try {
        editor.addParagraphFromJSON(paragraphJSON)
      } catch(e) {
        return;
      }

      if (!waveform.isPlaying()) {
        editor.scrollToEnd();
      }

      return true;
    };

    try {

    const textDecoder = new TextDecoder();
    const reader = response.body.getReader();

    let lastLine = '';
    for (;;) {
      const chunk = await reader.read();
      // console.log('got chunk:', chunk)
      const lines = textDecoder.decode(chunk.value).split("\n");
      // console.log('got lines:', lines)
      lastLine += lines[0];
      if(lines.length > 1) {
        if(lastLine.length > 0) {
          processLine(lastLine);
        }
        lastLine = lines[lines.length-1];
        // remaining lines (excludes first and last)
        for(let i=1; i<lines.length-1; i++) {
          if(lines[i].length > 0) {
            processLine(lines[i]);
          }
        }
      }
      if(chunk.done) {
        if(lastLine.length > 0) {
          // process last line
          processLine(lastLine);
        }
        break;
      }
    }
    } finally {
      if (!waveform.isPlaying()) {
          editor.scrollToStart();
      }
      rmclass(dom.editor, 'processing');
      hide(dom.abortButton);
    }
  }

  function setState(state, message) {
    if (!state || state === '') {
      select('.error').style.display = 'none';
      select('.loader').style.display = 'none';
    } else if (state === 'busy') {
      select('.error').style.display = 'none';
      select('.loader').style.display = 'block';
    } else if (state === 'error') {
      select('.loader').style.display = 'none';
      hide(dom.topSpinner);

      const errorContainer = select('.error');
      errorContainer.style.display = 'block';
      if (!message) {
        message = 'Processing failed';
      } else {
        message = `Processing failed, error: ${message}`;
      }
      errorContainer.textContent = message;
    }
  }


  // setup audio recorders with event handling

  const recorder = new AudioRecorder();     // simple audio recorder
  const recorder2 = new AudioRecorderVAD(); // audio recorder with Voice Activity Detection (VAD)

  recorder2.on('voicestart', () => {
    addclass(dom.recordingTime, 'voiceactivity');
  });

  recorder2.on('voiceend', async (audio, time) => {
    rmclass(dom.recordingTime, 'voiceactivity');
    addclass(dom.editor, 'processing');

    // setTimeout(() => {
    //   editor.dom.classList.remove('processing');
    // }, 2000)
    // return;

    let result;

    try {

      try {
        console.log(audio)
        const language = 'auto';
        const formData = new FormData();
        formData.append('input', audio, 'dummy');
        formData.append('lang', language);

        const response = await fetch(`./api/whisper`, { method: 'POST', body: formData, headers: { 'Accept': 'application/json' } });

        if (!response.ok) {
          if (response.status == 413) {
            // setState('error', 'payload too large');
          }
          // setState('error');
          return;
        }

        console.log(time)

        result = await response.json();

        for (const segment of result.segments) {
          for (const token of segment.tokens) {
            token.start += time;
            token.end += time;
          }
        }

      } catch(e) {
        console.log(e);
        setState('error', `upload failed`);
        // setState('error', `upload failed: ${e.message}`);
        return;
      }

      let doc;
      try {
        // console.log('whisper result:', result);
        const docJSON = whisperToNodes(result /*, cutoff - expose from inside extractAudio */);
        // console.log('doc json:', docJSON);
        // console.log(JSON.stringify(docJSON, null, 2));
        // doc = editorSchema.nodeFromJSON(docJSON);

        const currentDocJSON = editor.toJSON();

        console.log(currentDocJSON)
        if (((currentDocJSON.content.length ?? 0) > 0) && !currentDocJSON.content[currentDocJSON.content.length - 1].content?.length) {
          currentDocJSON.content.splice(currentDocJSON.content.length - 1, 1);
        }
        currentDocJSON.content = currentDocJSON.content.concat(docJSON.content);

        editor.fromJSON(currentDocJSON);
      } catch(e) {
        console.log(e);
        setState('error', e.message);
        return;
      }

    } finally {
      rmclass(dom.editor, 'processing');
    }
  });

  recorder2.on('start', () => {
    addclass(dom.recordButton, 'recording');
    addclass(dom.recordingTime, 'recording');
  });

  recorder2.on('stop', async (audio) => {
    rmclass(dom.recordingTime, 'voiceactivity');
    // editor.dom.classList.remove('processing');
    rmclass(dom.recordButton, 'recording');
    rmclass(dom.recordingTime, 'recording');
    console.log('got recorded audio', audio);
    // start spinner
    // setState('busy');

    if (audio && audio.size > 0) {
      addclass(dom.waveform, 'loading');
      hide(dom.waveformPlaceholder);
      try {
        // audio = await extractAudio(audio, 16000, 1, config.whisper.limit [# limit duration to N seconds #]);
        audio = await extractAudio(audio, 16000, 1);
        console.log('got recorded audio', audio);
      } catch(e) {
        console.log(e);
        setState('error', 'failed to extract audio');
        rmclas(dom.waveform, 'loading');
        return;
      }

      try {
        await waveform.loadBlob(audio);
        rmclass(dom.waveform, 'loading');
        setTimeout(() => {
          waveform.zoom(dom.zoomSlider.valueAsNumber);
          setPlaybackRate(1);
        }, 100);
      } catch(e) {
        console.log(e);
        setState('error', 'loading audio failed');
        rmclass(dom.waveform, 'loading');
        return;
      }
    }
  });

  recorder.on('start', () => {
    addclass(dom.recordButton, 'recording');
    addclass(dom.recordingTime, 'recording');
  });

  recorder.on('stop', async (audio) => {
    rmclass(dom.recordButton, 'recording');
    rmclass(dom.recordingTime, 'recording');
    console.log('got recorded audio', audio);
    // start spinner
    setState('busy');

    addclass(dom.waveform, 'loading');
    hide(dom.waveformPlaceholder);
    try {
      audio = await extractAudio(audio, 16000, 1, config.whisper.limit /* limit duration to N seconds */);
      console.log('got recorded audio', audio);
    } catch(e) {
      console.log(e);
      setState('error', 'failed to extract audio');
      rmclass(dom.waveform, 'loading');
      return;
    }

    try {
      await waveform.loadBlob(audio);
      rmclass(dom.waveform, 'loading');
      setTimeout(() => {
        waveform.zoom(dom.zoomSlider.valueAsNumber);
        setPlaybackRate(1);
      }, 100);
    } catch(e) {
      console.log(e);
      setState('error', 'loading audio failed');
      rmclass(dom.waveform, 'loading');
      return;
    }

    let result;

    try {
      const language = 'auto';
      const formData = new FormData();
      formData.append('input', audio, 'dummy');
      formData.append('lang', language);

      const response = await fetch(`./api/whisper`, { method: 'POST', body: formData, headers: { 'Accept': 'application/json' } });

      if (!response.ok) {
        if (response.status == 413) {
          setState('error', 'payload too large');
        }
        setState('error');
        return;
      }

      result = await response.json();

    } catch(e) {
      console.log(e);
      setState('error', `upload failed`);
      // setState('error', `upload failed: ${e.message}`);
      return;
    }

    let doc;
    try {
      // console.log('whisper result:', result);
      const docJSON = whisperToNodes(result /*, cutoff - expose from inside extractAudio */);
      // console.log('doc json:', docJSON);
      // console.log(JSON.stringify(docJSON, null, 2));
      editor.fromJSON(docJSON);
    } catch(e) {
      console.log(e);
      setState('error', e.message);
      return;
    }

    // stop spinner
    setState();
  });

  recorder.on('timeupdate', (time, timeString) => {
    dom.recordingTime.textContent = timeString;
  });

  recorder2.on('timeupdate', (time, timeString) => {
    dom.recordingTime.textContent = timeString;
  });

  // let recordDown = false;
  // onclick(dom.recordButton, (event) => {
  //   if (recordDown)
  //     return;
  //   recordDown = true;
  //   if (recorder.startStopRecording()) {
  //     // recording
  //   }
  //   recordDown = false;
  // });

  // let recordDown2 = false;
  // document.querySelector('#record2').addEventListener('click', (event) => {
  //   if (recordDown2)
  //     return;
  //   recordDown2 = true;
  //   if (recorder2.startStopRecording()) {
  //     // recording
  //   }
  //   recordDown2 = false;
  // });


  // setup two mode recorder button

  let recordButtonDown = false;
  let recordButtonLongPressTimer;
  let recordButtonLongPressDetected = false;
  let activeRecorder = undefined;

  on(dom.recordButton, 'mousedown', () => {
    if (recordButtonDown)
      return;
    recordButtonDown = true;
    recordButtonLongPressDetected = false;
    // Start the recordButtonLongPressTimer
    recordButtonLongPressTimer = setTimeout(() => {
      recordButtonLongPressDetected = true;
    }, 1000); // 1000 milliseconds = 1 second
  });

  on(dom.recordButton, 'mouseup', async () => {
    if (!recordButtonDown) {
      return;
    }
    // Clear the recordButtonLongPressTimer
    clearTimeout(recordButtonLongPressTimer);
    try {
      if (activeRecorder === 'recorder') {
        await recorder.startStopRecording();
        activeRecorder = null;
        return;
      } else if (activeRecorder === 'recorder2') {
        await recorder2.startStopRecording();
        activeRecorder = null;
        return;
      }
      if (!recordButtonLongPressDetected) {
        // long
        activeRecorder = 'recorder';
        await recorder.startStopRecording();
      } else {
        // short
        activeRecorder = 'recorder2';
        await recorder2.startStopRecording();
      }
    } finally {
      recordButtonDown = false;
    }
  });

  on(dom.recordButton, 'mouseleave', () => {
    recordButtonDown = false;
    // Clear the recordButtonLongPressTimer if the mouse leaves the recordButton
    clearTimeout(recordButtonLongPressTimer);
  });

  on(dom.recordButton, 'touchstart', (e) => {
    e.preventDefault(); // Prevent default touch behavior
    if (recordButtonDown)
      return;
    recordButtonLongPressDetected = false;
    recordButtonLongPressTimer = setTimeout(() => {
      recordButtonLongPressDetected = true;
      if (navigator.vibrate) {
        navigator.vibrate([100]);
      }
    }, 1000);
  });

  on(dom.recordButton, 'touchend', async () => {
    clearTimeout(recordButtonLongPressTimer);
    try {
      if (activeRecorder === 'recorder') {
        await recorder.startStopRecording();
        activeRecorder = null;
        return;
      } else if (activeRecorder === 'recorder2') {
        await recorder2.startStopRecording();
        activeRecorder = null;
        return;
      }
      if (!recordButtonLongPressDetected) {
        // long
        activeRecorder = 'recorder';
        await recorder.startStopRecording();
      } else {
        // short
        activeRecorder = 'recorder2';
        await recorder2.startStopRecording();
      }
    } finally {
      recordButtonDown = false;
    }
  });



  // listen for global keydown events (alternatively listen may on the document object)
  on(window, 'keydown', function(event) {
    // console.log('keydown:', event);
    if (event.key == 'Escape') {
      if (hasclass(dom.rootContainer, 'menu-open')) {
        rmclass(dom.menu, 'open');
        rmclass(dom.overlay, 'open-overlay');
        rmclass(dom.rootContainer, 'menu-open');
      } else {
        playPause();
      }
    }
  });

  onclick(dom.abortButton, async () => {
    if (documentID && documentID.length == 6) {
      const response = await fetch(`./api/whisper/${documentID}/abort`);
      // const status = await response.text();
      if (response.ok) {
        // TODO: new call to /wait or reuse any existing ongoing
        hide(dom.abortButton);
      }
    } else if (processingID) {
      const response = await fetch(`./api/whisper/${processingID}/abort`);
      // const status = await response.text();
      if (response.ok) {
        // TODO: new call to /wait or reuse any existing ongoing
        hide(dom.abortButton);
      }
    }
  });

  async function loadDocument(documentID) {

    if (documentID && documentID.length == 6) {

      const response = await fetch(`./api/whisper/${documentID}/status`);
      const status = await response.text();
      console.log('job status:', status);



      await collectResults(documentID);

      // stop spinner
      // setState('busy');
      // setState();

      unhide(dom.saveButton);

    } else if (documentID) {
      setState('busy');

      let doc;
      try {
        const response = await fetch(`./api/storage/${documentID}`)
        const result = await response.json();

        // console.log('whisper result:', result);
        const docJSON = result.type == 'doc' ? augmentDocJSON(result) : whisperToNodes(result /*, cutoff - expose from inside extractAudio */);
        // console.log('doc json:', docJSON);
        // console.log(JSON.stringify(docJSON, null, 2));

        documentOwner = await documentVerifyKey();

        editor.fromJSON(docJSON);
      } catch(e) {
        console.log(e);
        setState('error', e.message);
        return;
      }

      editor.setStateID(documentID);

      if (documentOwner) {
        unhide(dom.removeDocument);
      } else {
        hide(dom.removeDocument);
      }

      // stop spinner
      setState();

      // unhide(dom.saveButton);

      try {
        const response = await fetch(`./api/storage/${documentID}/audio`)
        if (!response.ok)
          throw new Error(`audio fetch error, response status: ${response.status}`);

        const audio = await response.blob();

        try {
          await waveform.loadBlob(audio);
          rmclass(dom.waveform, 'loading');
          setTimeout(() => {
            waveform.zoom(dom.zoomSlider.valueAsNumber);
            setPlaybackRate(1);
          }, 300);
        } catch(e) {
          console.log(e);
          setState('error', 'loading audio failed');
          rmclass(dom.waveform, 'loading');
          hide(dom.topSpinner);
        }

      } catch (e) {
        console.error('unable to fetch audio:', e);
      }
    }

    loadAudioTitle();

    documentChanged = false;
  }

  let documentID = location.hash.length > 0 ? location.hash.substr(1) : undefined;
  // let documentChanged = false;

  onclick(dom.waveformPlaceholder, (event) => {
    startNewDocument = false;
    dom.fileSelector.click();
  });

  unhide(dom.waveformPlaceholder, 'hidden');

  async function documentVerifyKey(id, key) {
    if (!id) {
      id = documentID;
    }
    if (!key) {
      key = theKey;
    }
    const response = await fetch(`./api/storage/${id}/verify?key=${theKey}`, { method: 'GET', headers: { 'Accept': '*/*' } });
    if (response.ok) {
      return true;
    }
    return false;
  }

  async function saveDocument(id, key) {
    const docJSON = JSON.stringify(editor.toJSON(), null, 2);
    const uuid = id ? id : crypto.randomUUID();
    key = key || theKey;

    const response = await fetch(`./api/storage/${uuid}?key=${key}`,
      { method: 'PUT', body: docJSON, headers: { 'Accept': 'application/json', 'Content-Type': 'application/octet-stream' } });

    if (!response.ok) {
      if (response.status == 413) {
        console.error('payload too large');
      } else {
        console.error(`upload returned status ${response.status}`);
      }
      return false;
    }
    // result = await response.text();

    return uuid;
  }

  async function saveAudio(uuid, key) {
    key = key || theKey;

    if (waveform.getDuration() == 0)
      return false;

    let audio;
    console.log('storing audio')
    const audioBuffer = waveform.getDecodedData();
    audio = buffer2wav(audioBuffer);

    const response = await fetch(`./api/storage/${uuid}/audio?key=${key}`,
      { method: 'PUT', body: audio, headers: { 'Content-Type': 'audio/wav' } });

    if (!response.ok) {
      if (response.status == 413) {
        console.error('payload too large');
      } else {
        console.error(`upload returned status ${response.status}`);
      }
      return false;
    }
    // result = await response.text();

    return true;
  }

  async function removeDocument(id, key) {
    const uuid = id || documentID;
    key = key || theKey;

    const response = await fetch(`./api/storage/${uuid}?key=${key}`, { method: 'DELETE', headers: { 'Accept': '*/*' } });

    if (!response.ok) {
      if (response.status == 404 || response.status == 500) {
        console.error(`unable to delete document, status = ${response.status}`);
      }
      return false;
    }
    return true;
  }

  onclick(dom.saveButton, async (event) => {
    if (documentOwner) {
      const uuid = await saveDocument(documentID);
      if (uuid) {
        // window.open(`/#${uuid}`, '_blank');
        hide(dom.saveButton);  // TODO: ...
        editor.setStateID(documentID);
        onDocumentChanged();
      }
    }
  });

  function loadAudioTitle() {
    if (documentID) {
      unhide('#waveform-placeholder > span.load');
      hide('#waveform-placeholder > span.upload');
    } else {
      hide('#waveform-placeholder > span.load');
      unhide('#waveform-placeholder > span.upload');
    }
  }

  onclick('#clear-document', async (event) => {
    editor.clear();
    documentID = '';
    location.hash = '';
    hide(dom.saveButton);  // TODO: ...
    closeMenu();
  });

  onclick(dom.removeDocument, async (event) => {
    if (documentID && documentOwner) {
      if (removeDocument()) {
        // editor.clear();
        documentID = '';
        location.hash = '';
        hide(dom.saveButton);  // TODO: ...
      }
      closeMenu();
      onDocumentChanged();
    }
  });


  onclick('#open-new', async (event) => {
    // TODO: check if localhost
    // window.open('/', '_blank')
    window.open('https://late.ailab.lv', '_blank')
    closeMenu();
  });

  onclick('#open-new-ltg', async (event) => {
    window.open('https://ltg.late.ailab.lv', '_blank')
    closeMenu();
  });

  onclick(dom.copyLink, async (event) => {
    copyTextToClipboard(location.href);
  });

  function closeMenu() {
    rmclass(dom.menu, 'open');
    rmclass(dom.overlay, 'open-overlay');
    rmclass(dom.rootContainer, 'menu-open');
  }

  function openMenu() {
    if (documentID) {
      console.log('open menu has document', documentID)
      unhide(dom.copyLink);
    } else {
      console.log('open menu has NO document', documentID)
      hide(dom.copyLink);
    }
    addclass(dom.menu, 'open');
    addclass(dom.overlay, 'open-overlay');
    addclass(dom.rootContainer, 'menu-open');
  }

  onclick('#menu-button', function() {
    openMenu();
  });

  onclick('#close-menu', function() {
    closeMenu();
  });

  onclick(dom.overlay, function() {
    closeMenu();
  });

  function monitorSwipeGestures() {
    const root = dom.rootContainer;
    const menu = dom.menu;
    const overlay = dom.overlay;

    let startX, startY;

    const el = root;

    let menuOpen = false;

    on(el, 'touchstart', function(event) {
      if (event.target.closest('.no-swipe')) {
        return;
      }
      startX = event.touches[0].clientX;
      startY = event.touches[0].clientY;
      menuOpen = hasclass(root, 'menu-open');
    });

    on(el, 'touchmove', function(event) {
      if (!startX) return;
      if (!startY) return;

      let currentX = event.touches[0].clientX;
      let currentY = event.touches[0].clientY;
      let diffX = currentX - startX;
      let diffY = currentY - startY;

      if (Math.abs(diffY) >= 50 && Math.abs(diffY) >= Math.abs(diffX) * 2) {
        startX = null;
        startY = null;
        return;
      }

      // Detect swipe right
      if (menuOpen && diffX > 80) { // Adjust threshold as needed
          closeMenu();
          startX = null; // Reset startX after swipe is detected

          event.stopPropagation();
          event.preventDefault();
          return true;
      } else if (!menuOpen && diffX < -80) { // Adjust threshold as needed
          openMenu();

          event.stopPropagation();
          event.preventDefault();
          return true;
      }

      return false;
    });

    on(el, 'touchend', function() {
      startX = null;
    });
  }

  onclick(dom.shareButton, async () => {
    const currentStateID = editor.getStateID();
    if (currentStateID) {
      // only copy the link
      copyTextToClipboard(location.href);
    } else {
      editor.setStateID(documentID);
      // generate new id
      // store document with the id
      const uuid = await saveDocument();
      if (uuid) {
        documentID = uuid;
        documentOwner = true;
        editor.setStateID(uuid);
        location.hash = documentID;
        copyTextToClipboard(location.href);
        unhide(dom.removeDocument);
        await saveAudio(uuid);
      }
      // update icon
      rmclass(dom.shareButton, 'fill');
    }
  });

  function onDocumentChanged() {
    console.log('document changed')
    const currentStateID = editor.getStateID();
    if (currentStateID) {
      rmclass(dom.shareButton, 'fill');
    } else {
      addclass(dom.shareButton, 'fill');
    }
    if (documentID && documentOwner && !currentStateID) {
      unhide(dom.saveButton);
    } else {
      hide(dom.saveButton);
    }
    // console.log('Current State ID:', currentStateID);
  }

  monitorSwipeGestures();

  on(document, 'touchstart', function() {}, {passive: true});

  loadDocument(documentID);

  onDocumentChanged();

  // debug
  window.setID = (id) => {
    editor.setStateID(id);
      const currentStateID = editor.getStateID();
      console.log('Current State ID:', currentStateID);
  };
  window.setTime = (t) => { editor.setTime(t); };
}


// generate random id
let theKey = localStorage.getItem('key');
if (!theKey) {
  theKey = generateRandomId(4);
  localStorage.setItem('key', theKey);
}


if (document.readyState === 'complete' || document.readyState === 'interactive') {
    main();
} else {
    on(document, 'DOMContentLoaded', main);
}
