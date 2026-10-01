(function_declaration
  "func" @context
  name: (identifier) @name) @item

(global_declaration
  "global" @context
  name: (identifier) @name) @item

(assignment
  left: (identifier) @name
  right: [(function) (frame)]) @item

(slot
  name: (slot_name) @name
  value: [(function) (frame)]) @item

