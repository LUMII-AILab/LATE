import { Plugin, PluginKey } from "prosemirror-state";

export const stateIDPluginKey = new PluginKey("stateIDPlugin");

// Function to compare two ProseMirror documents deeply
function areNodesEqual(node1, node2) {
  if (node1.type !== node2.type) return false;
  if (!node1.sameMarkup(node2)) return false;
  if (node1.childCount !== node2.childCount) return false;
  if (node1.text !== node2.text) return false;

  for (let i = 0; i < node1.childCount; i++) {
    if (!areNodesEqual(node1.child(i), node2.child(i))) return false;
  }
  return true;
}

export const stateIDPlugin = new Plugin({
  key: stateIDPluginKey,
  state: {
    init(config, instance) {
      return { };
      // return { rev: 0, value: null, idmap: {}, idstack: [] };
    },
    apply(tr, pluginState, oldState, newState) {
      let { id, lastID, doc, pos } = pluginState;

      // TODO: keep track of many id's

      const newValueMeta = tr.getMeta(stateIDPluginKey);

      // pos >= 0 is past last stored id / future
      // pos = -1

      if (newValueMeta) {
        lastID = newValueMeta;
        doc = oldState.doc;
        id = lastID;
        pos = 0;
      } else if (doc && tr.docChanged) {
        const historyMeta = tr.getMeta("history$");
        if (historyMeta) {
          if (historyMeta.redo) {
            if (pos == 0) {
              pos = 1;
              id = undefined;
            } else if (pos < 0) {
              if (areNodesEqual(newState.doc, doc)) {
                pos = 0;
                id = lastID;
              }
            }
          } else {
            // undo
            if (pos == 0) {
              pos = -1;
              id = undefined;
            } else if (pos > 0) {
              if (areNodesEqual(newState.doc, doc)) {
                pos = 0;
                id = lastID;
              }
            }
          }
        } else {
          if (pos < 0) {
            // we are loosing the document state
            doc = undefined;
            lastID = undefined;
            id = undefined;
          } else if (pos >= 0) {
            pos = 1;
            id = undefined;
          }
        }
      }

      return { id, lastID, doc, pos };
    }
  }
});

export function setStateID(view, id) {
  view.dispatch(
    view.state.tr.setMeta(stateIDPluginKey, id)
  );
}

export function getStateID(view) {
  return stateIDPluginKey.getState(view.state).id;
}
