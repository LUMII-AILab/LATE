import { Schema } from 'prosemirror-model';


export const editorSchema = new Schema({
  nodes: {
    doc: { content: "block+" },
    paragraph: {
      content: "inline*",
      group: "block",
      attrs: {
        start: { default: "--" }, // start timestamp
        end: { default: "--" },   // end timestamp
      },
      parseDOM: [{
        tag: "p",
        getAttrs(dom) {
          return {
            start: parseInt(dom.getAttribute("data-start")),
            end: parseInt(dom.getAttribute("data-end"))
          };
        }
      }],
      toDOM: (node) => ['p', {
        'data-start': node.attrs.start,
        'data-end': node.attrs.end,
      }, 0]
    },
    text: {
      group: "inline",
    },
    span: {
      inline: true,
      group: "inline",
      content: "text*",
      attrs: {
        start: { default: null },
        end: { default: null },
        highlighted: { default: false },
        p: { default: 1 }
      },
      toDOM(node) {
        const { p } = node.attrs;
        return ["span", {
          "data-start": node.attrs.start,
          "data-end": node.attrs.end,
          "data-p": node.attrs.p,
          "style": `--value: ${p};`,
          "class": 'dynamic-color ' + (node.attrs.highlighted ? "highlight" : '')
        }, 0];
      },
      parseDOM: [{
        tag: "span[data-start][data-end]",
        getAttrs(dom) {
          return {
            start: parseInt(dom.getAttribute("data-start")),
            end: parseInt(dom.getAttribute("data-end")),
            p: parseInt(dom.getAttribute("data-p")),
            highlighted: dom.classList.contains("highlight")
          };
        }
      }]
    },
  },
  marks: {},
});
