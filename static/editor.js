import { schema } from 'prosemirror-schema-basic';
import { EditorState, TextSelection, NodeSelection, Plugin, PluginKey, Transaction } from 'prosemirror-state';
import { EditorView, Decoration, DecorationSet } from 'prosemirror-view';
import { Schema, DOMParser as ProseMirrorDOMParser, MarkType, Fragment, Slice, NodeRange } from 'prosemirror-model';
import { undo, redo, history } from 'prosemirror-history';
import { keymap } from 'prosemirror-keymap';
import { baseKeymap, chainCommands, newlineInCode, createParagraphNear, liftEmptyBlock, splitBlock, deleteSelection } from 'prosemirror-commands';
import { addListNodes } from 'prosemirror-schema-list';
import { ReplaceStep, ReplaceAroundStep } from 'prosemirror-transform';
import { findSelectedNodeOfType, findParentNode, findParentNodeOfType, findParentNodeOfTypeClosestToPos } from 'prosemirror-utils';

import { editorSchema } from './schema.js';
import { stateIDPlugin, setStateID, getStateID } from './stateid.js';
import { setWaveformTime } from './waveform.js';
import { throttled, generateRandomId } from './utils.js';


export function scrollToStart(view) {
  view.dom.parentNode.scrollTo({ top: 0, behavior: "smooth" });
  return;
}

export function scrollToEnd(view) {
  view.dom.parentNode.scrollTo({ top: view.dom.parentNode.scrollHeight, behavior: "smooth" });
  return;

  const endPos = view.state.doc.content.size-1;
  const resolvedPos = view.state.doc.resolve(endPos);
    const node = view.domAtPos(resolvedPos.pos).node;
  console.log('NODE', node)
  console.dir(node)
    // if (node && isElementOutOfView(node)) {
      node.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
      // node.scrollIntoView({ behavior: 'smooth', block: 'end' });
    // }
  // const tr = view.state.tr.setSelection(EditorState.selection(view.state, resolvedPos));
  // view.dispatch(tr.scrollIntoView());
}


// Utility function to check if an element is out of view
function isElementOutOfView(el) {
  return true;
  const rect = el.getBoundingClientRect();
  const windowHeight = (window.innerHeight || document.documentElement.clientHeight);
  const windowWidth = (window.innerWidth || document.documentElement.clientWidth);

  const vertInView = (rect.top <= windowHeight) && ((rect.top + rect.height) >= 0);
  const horInView = (rect.left <= windowWidth) && ((rect.left + rect.width) >= 0);

  return !(vertInView && horInView);
}

const scrollNodeIntoView = throttled(async (node) => {
  // console.log('scroll node into view', node)
    node.scrollIntoView({ behavior: 'smooth', block: 'center' });
}, 1000);


// Function to find and scroll the span node into view if it's out of view
function scrollSpanNodeIntoView(view, currentTime) {
  const { state } = view;
  const { doc } = state;
  let targetPos = null;

  doc.descendants((node, pos) => {
    if (node.type.name === 'span' && node.attrs.start <= currentTime && node.attrs.end >= currentTime) {
      targetPos = pos;
      return false; // Stop traversal
    }
    return true; // Continue traversal
  });

  // console.log('scroll span targetpos', targetPos)
  if (targetPos !== null) {
    const resolvedPos = state.doc.resolve(targetPos);
  // console.log('scroll span resolvedpos', resolvedPos)
    const node = view.domAtPos(resolvedPos.pos).node;
  // console.log('scroll span node', node)
    if (node && isElementOutOfView(node)) {
      // node.scrollIntoView({ behavior: 'smooth', block: 'center' });
      // console.log('scroll node', node)
      scrollNodeIntoView(node);
    }
  }
}

// // Example of setting up an interval to check currentTime and scroll the node into view
// setInterval(() => {
//   // Assuming 'view' is your ProseMirror editor view
//   scrollSpanNodeIntoView(view, currentTime);
// }, 1000); // Adjust the interval as needed
//
// // Example of updating currentTime (this should be done based on your application's logic)
// function updateCurrentTime(newTime) {
//   currentTime = newTime;
// }


export let currentTime = 0;  // Initial time setting

export function getCurrentTime() {
  return currentTime
}

function applyTimeDecorations(doc, currentTime) {
  const decorations = [];
  doc.descendants((node, pos) => {
    if (node.type.name === "span") {
      const { start, end } = node.attrs;
      let className = "";
      if (currentTime >= start && currentTime < end) {
        className = "highlight"; // Current time is within the span's range
      } else if (currentTime < start) {
        className = "time-before"; // Current time is before the span's start time
      } else if (currentTime > end) {
        className = "time-after"; // Current time is after the span's end time
      }

      if (className) {
        const decoration = Decoration.node(pos, pos + node.nodeSize, { class: className });
        decorations.push(decoration);
      }
    }
  });
  return DecorationSet.create(doc, decorations);
}

function timeHighlightingPlugin() {
  // let currentTime = 0; // You'll need mechanisms to update this dynamically

  return new Plugin({
    state: {
      init(_, { doc }) {
        return applyTimeDecorations(doc, currentTime);
      },
      apply(tr, oldDecorationSet, oldState, newState) {
        if (tr.docChanged || tr.getMeta('updateTime')) {
          return applyTimeDecorations(newState.doc, currentTime);
        }
        return oldDecorationSet;
      }
    },
    props: {
      decorations(state) {
        return this.getState(state);
      }
    }
  });
}


export function setCurrentTime(view, newTime) {
  currentTime = newTime; // Update the global time variable
  view.dispatch(view.state.tr.setMeta('updateTime', true)); // Signal to update decorations
  scrollSpanNodeIntoView(view, newTime);
}


// Helper function to dive into the paragraph from a given start index
function diveIntoParagraph(paragraph, startIndex) {
  if (startIndex < 0) {
    for (let i = paragraph.childCount - 1; i >= 0; i--) {
        if (paragraph.child(i).type.name === 'span') {
            return paragraph.child(i);
        }
    }
    return null;
  } else {
    for (let i = startIndex; i < paragraph.childCount; i++) {
        if (paragraph.child(i).type.name === 'span') {
            return paragraph.child(i);
        }
    }
    return null;
  }
}


function findNearestSpan($pos) {
    // Check if the direct parent or even the position itself is a 'span'
    for (let depth = $pos.depth; depth > 0; depth--) {
        const node = $pos.node(depth);
        if (node.type.name === 'span') {
            return node; // Returns the span node if the position is within a span
        }
    }

    // Check nodes immediately before and after the position
    const nodeBefore = $pos.nodeBefore;
    const nodeAfter = $pos.nodeAfter;

    if (nodeBefore && nodeBefore.type.name === 'span') {
        return nodeBefore;
    } else if (nodeAfter && nodeAfter.type.name === 'span') {
        return nodeAfter;
    }

    if (nodeAfter && nodeAfter.type.name === 'paragraph') {
        // const indexAfter = $pos.indexAfter($pos.depth);
        return diveIntoParagraph(nodeAfter, 0);
    } else if (nodeBefore && nodeBefore.type.name === 'paragraph') {
        // const indexBefore = $pos.indexBefore($pos.depth);
        return diveIntoParagraph(nodeBefore, -1);
    }

    // Expand search to siblings before going up the tree if no 'span' found directly adjacent
    let depth = $pos.depth;
    while (depth > 0) {
        const parent = $pos.node(depth);
        const indexBefore = $pos.index(depth) - 1;
        const indexAfter = $pos.indexAfter(depth);

        // Check siblings before the current position
        for (let i = indexBefore; i >= 0; i--) {
            const siblingBefore = parent.child(i);
            if (siblingBefore.type.name === 'span') {
                return siblingBefore;
            }
        }

        // Check siblings after the current position
        for (let i = indexAfter; i < parent.childCount; i++) {
            const siblingAfter = parent.child(i);
            if (siblingAfter.type.name === 'span') {
                return siblingAfter;
            }
        }

        // If at the boundary of a paragraph, dive into it
        if (parent.type.name === 'paragraph') {
            const spanInParagraph = diveIntoParagraph(parent, indexAfter);
            if (spanInParagraph) return spanInParagraph;
        }

        // Reduce the depth after checking siblings
        depth--;
    }

    return null; // Return null if no 'span' is found at any level
}


const optionClickPlugin = new Plugin({
    props: {
        handleDOMEvents: {
            click: (view, event) => {
                // if (!event.altKey) {
                //   return false;
                // }
                    // console.log('Option-click inside ProseMirror editor!');
                // Get the position in the document from the mouse coordinates
                const coords = { left: event.clientX, top: event.clientY };
                const pos = view.posAtCoords(coords);

                // Check if a valid position was returned
                if (pos && pos.pos !== null) {
                    let $pos = view.state.doc.resolve(pos.pos);
                  // console.log($pos)

                    // Attempt to find the nearest span using nodeAfter or nodeBefore
                    let nearestSpan = findNearestSpan($pos);
                    if (nearestSpan) {
                        console.log("Nearest span node: ", nearestSpan);
                      const currentTime = nearestSpan.attrs.start;
                      // setCurrentTime(view, currentTime)
                      setWaveformTime(currentTime/100);
                        // Perform any additional actions with the found 'span' node
                    }
                } else {
                    console.log("Click position not inside the document");
                }
                return false; // Continue with the default click handling

                    console.log('Option-click inside ProseMirror editor!');
                    // customEditorFunction(view, event);

                // let coords = { left: event.clientX, top: event.clientY };
                // let pos = view.posAtCoords(coords);
                //
                // if (pos) {
                //   console.log(pos)
                //     let nearestSpan = findNearestSpan(view.state, pos.pos);
                //     if (nearestSpan) {
                //         console.log("Nearest span node: ", nearestSpan);
                //         // Perform any additional actions with the found 'span' node
                //     }
                // }
                //
                //
                //     return true; // Indicates that this event should not be propagated further
            }
        }
    }
});


function customEnterCommand(state, dispatch) {

  const { $from, $to } = state.selection;

  if ($from.pos !== $to.pos) {

    // Create a transaction
    let tr = state.tr;
    console.log('FROM, TO', $from, $to)

    // Start the transaction by deleting the selection if it exists
    tr.deleteSelection();

    // Find the position to split the paragraph
    let splitPos = tr.selection.from; // After deletion, from and to should be the same
    console.log(splitPos)

    // Split the paragraph at the cursor position
    // No need to insert a new paragraph; `split` will handle this
     tr.split(splitPos, 2);

    // If you need to ensure that the new paragraph is empty, you can adjust the transaction
    // For example, if splitting mid-paragraph, ensure text moves with the cursor

    // Apply the transaction
    if (dispatch) {
        dispatch(tr.scrollIntoView());
    }
    return true;
  }


  // if ($from.pos == $to.pos) {
  // if (!state.selection.empty) {
  //   if (!deleteSelection(state, dispatch))
  //     return false;
  // }
    // state = state.apply(tr);
    // console.log('applied')
    // tr = state.tr;
    //   dispatch(tr.scrollIntoView());
      // return true;

  const {selection, schema} = state;

  //   console.log(state)
  // console.log('SELECTION', selection)
  // console.log('SELECTION $from', $from);
  // console.log('SELECTION $to', $to);

    let tr = state.tr;

    if ($from.parent.type === schema.nodes.paragraph) {

      // split paragraph into two
      const leftAttrs = { };  // e.g., id: whatever, ...
      const rightAttrs = { };

      tr.split($from.pos, 1, [{type: $from.parent.type, attrs: rightAttrs }])
      tr.setNodeMarkup($from.before(), null, leftAttrs);
      // tr.setNodeMarkup($from.start($from.depth - 1[#why?#]), null, rightAttrs);

      dispatch(tr.scrollIntoView());
      return true;

    } else if ($from.parent.type.name === 'span') {
      let tr = state.tr;

      console.log('SPAN SPLIT pos', $from)

      const leftAttrs = { ...$from.parent.attrs };
      const rightAttrs = { ...$from.parent.attrs };

      console.log(leftAttrs, rightAttrs)

      const textSplitRatio = ($from.pos - $from.start()) / $from.parent.textContent.length;

      const splitTime = leftAttrs.start + (rightAttrs.end - leftAttrs.start) * textSplitRatio;
      console.log(splitTime)

      leftAttrs.end = splitTime;
      rightAttrs.start = splitTime;
      console.log(leftAttrs, rightAttrs)

      console.log('node text:', $from.parent.textContent);
      console.log($from.pos, $from.start(), $from.start($from.depth-1))

      // split span
      tr.split($from.pos, 1, [
        {type: $from.parent.type, attrs: rightAttrs},
      ]);
      tr.setNodeMarkup($from.before(), null, leftAttrs);
      // console.log('node text:', $from.parent.text);
      // console.log('node at start(depth-1)', state.doc.nodeAt($from.start($from.depth-2)))

      // console.log($from.pos, $from.start($from.depth))
      // console.log('node at pos', state.doc.nodeAt($from.pos))
      // console.log('node at start(depth-1)', state.doc.nodeAt($from.start($from.depth-2)))
      // tr.setNodeMarkup($from.pos+1, null, { auto: '', manual: '', id: generateRandomId() });
      // tr.setNodeMarkup($from.start($from.depth), null, { auto: '', manual: '', id: generateRandomId() });
      // console.log('ssplit', $from.pos+1, $from.before())

      tr.split($from.pos+1, 1, [{type: schema.nodes.paragraph, attrs: { id: 'sentX' /* generateRandomId() */ }}]); // split paragraph
      tr.setNodeMarkup($from.before($from.depth - 1), null, { id: 'sentY' /* generateRandomId() */ });

      // const newParagraphNode = schema.nodes.paragraph.create();

      // schedule_transcribe(rightAttrs.id, state.doc.nodeAt($from.pos).text.substring($from.textOffset));

      // TODO: schedule transcribe here

      dispatch(tr.scrollIntoView());
      return true;
    }
}


export function clearDocument(editorView) {
  const emptyDoc = editorSchema.nodeFromJSON({ type: "doc", content: [] });
  const tr = editorView.state.tr.replaceWith(0, editorView.state.doc.content.size, emptyDoc);
  editorView.dispatch(tr);
}


let editorView;


export async function setupEditor(settings) {
  const { playPause, onDocumentChanged } = settings;
  let { container } = settings;

  if (typeof container === 'string') {
    container = document.querySelector(container);
  }

  function playPauseKeyCommand(state, dispatch, view) {
    // Optionally, you can use dispatch and view to manipulate the editor if needed
    playPause();
    return true;  // Prevent ProseMirror from processing this key event further
  }

  // Create key bindings
  const customKeymap = keymap({
    'Mod-z': undo,
    'Mod-y': redo,
    'Mod-Shift-z': redo,
    // 'Enter': splitBlock,
    'Enter': customEnterCommand,
    'Shift-Alt-Space': playPauseKeyCommand,
    'Alt-Space': playPauseKeyCommand,
    'Shift-Space': playPauseKeyCommand,
    // 'Escape': playPauseKeyCommand,
  });

  let state = EditorState.create({
    doc: undefined,
    schema: editorSchema,
    plugins: [
      stateIDPlugin,
      // highlightSpansPlugin(),
      // timeHighlightPlugin(),
      timeHighlightingPlugin(),
      optionClickPlugin,
      history(),
      customKeymap,
      keymap(baseKeymap) // Adds the default key bindings such as Enter, Backspace, etc.
    ]
  });

  editorView = new EditorView(container, {
    state,
    nodeViews: {
      // span: (node, view, getPos) => new SpanNodeView(node, view, getPos),
      // segment: (node, view, getPos) => new SegmentNodeView(node, view, getPos),
      // speaker: (node, view, getPos) => new SpeakerNodeView(node, view, getPos),
      // paragraph: (node, view, getPos) => new ParagraphNodeView(node, view, getPos),
    },
    dispatchTransaction(transaction) {
      // console.log('transaction.doc before', transaction.doc);
      // console.log("Document size went from", transaction.before.content.size,
      //             "to", transaction.doc.content.size)


      const newState = editorView.state.apply(transaction)
      editorView.updateState(newState)

      if (transaction.docChanged) {
        onDocumentChanged();
      }

      // console.log('transaction.doc after', transaction.doc);

      // if (!transaction.getMeta("immutable")) {
      //   if (transaction.docChanged) {
      //     console.log('SCHED', transaction)
      //     transcribeUnannotated(view);
      //   }
      // } else {
      //   console.log('CANCEL')
      //   transcribeUnannotated.cancel();
      // }
    }
  })

  return {
    editorView,
    dom: editorView.dom,
    container: container,
    // scrollToStart,
    // scrollToEnd,
    // setCurrentTime,
    getTime: getCurrentTime,
    clear() { clearDocument(editorView); },
    setTime(time) { return setCurrentTime(editorView, time); },
    scrollToStart() { return scrollToStart(editorView); },
    scrollToEnd() { return scrollToEnd(editorView); },
    toJSON() { return editorView.state.doc.toJSON(); },
    getText() {
      const doc = editorView.state.doc;
      return doc.textBetween(0, doc.content.size, "\n");
    },
    setStateID(id) { setStateID(editorView, id); },
    getStateID() { return getStateID(editorView); },
    fromJSON(docJSON) {
      let doc;
      try {
        doc = editorSchema.nodeFromJSON(docJSON);
      } catch(e) {
        console.log(e);
        throw new Error('invalid response');
      }
      try {
        const state = EditorState.create({
          doc: doc,
          plugins: editorView.state.plugins
        });
        editorView.updateState(state);
      } catch(e) {
        console.log(e);
        throw new Error('internal editor error');
      }
    },
    addParagraphFromJSON(paragraphJSON) {

      const state = editorView.state;

      const { schema } = state;

      let paragraph;
      try {
        paragraph = schema.nodeFromJSON(paragraphJSON);

        console.log('paragraph node:', paragraph);
      } catch(e) {
        console.log(e);
        throw new Error('invalid response');
      }

      try {
        // Get the document
        const doc = state.doc;

        // Find the last node in the document
        const lastNode = doc.lastChild;

        let tr = state.tr;

        // Check if the last node is a paragraph and is empty
        if (lastNode.type.name === 'paragraph' && lastNode.content.size === 0) {
          // Replace the empty last paragraph with the new paragraph
          tr = tr.replaceWith(doc.content.size - lastNode.nodeSize, doc.content.size, paragraph);
        } else {
          // Append the new paragraph to the end of the document
          tr = tr.insert(doc.content.size, paragraph);
        }

        // tr.insert(tr.doc.content.size, paragraph);
        editorView.dispatch(tr);

      } catch(e) {
        console.log(e);
        throw new Error('internal editor error');
      }
    },
  };
}
