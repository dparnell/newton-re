# Card items: an application's data on a Names card

*2026-11-27, the owner's ask: Do not disturb's setting on a contact's card,
and a way for any application to put its own elements on a card - shown
and edited there.  Code: `src/host/HostCardItems.ns` (the helper),
`romsrc/rex/Cardfile/objects/system/export18.ns` and
`romsrc/rex/Cardfile/objects/apps/cardfile.ns` (the card face, DEVIATIONs),
`src/host/HostQuickSheet.ns` (`dndCardItem`, the first item).*

## The design: Names' own extension, and one addition

**NewtonOS 2.x already has the system**, as Apple documented it for
packages (established by reading the Cardfile ROM extension in
`romsrc/rex/Cardfile/`, and tried: the prototype and `phone.ns` below).
A Names card's data is shown and edited through *info views* - Name,
Phone, Address, Custom, ... - each a view definition of the card's class
with `type: 'editor`.  Names looks them up with `GetViewDefs(class)`, so a
view definition a package registers (`RegisterViewDef(viewDef, 'person)`)
is one of them:

- **Add** (the status bar's) offers it when the card has none of its data
  (`AddLayout`: a single-valued item only when empty; an item kept in an
  array always);
- **All Info** shows its data (`kViewsToDisplay` - the class's
  `viewsToDisplay` plus every other `'editor` view - drawn by the
  text-lines view @8242, the data formatted by the definition's
  `infoFrame`);
- **a tap** on it there opens the **edit slip** on it (`ShowEditSlip`),
  whose body is the view definition itself - or runs its `actionFunc`;
- **Find and routing** see its text (`kGenericTextFunc`).

So the host does not add a second system: an application registers a view
definition, as it would on a MessagePad, and a package written for a real
Newton that adds Names data works here unchanged.

**The one addition - the card's face** (DEVIATION, the owner's choice):
the ROM's business cards show only the name, company, addresses and
phones (fixed 240x160 layouts), so such an item was seen only in All Info.
A view definition that says **`showOnCard: true`** is also shown **under
the business card**, in the Names card view (`export18Item_3`, a
text-lines view as All Info's: a bullet, the infoFrame's text, a tap
opening its editor).  The card view is made taller for it
(`cardfile:CardFaceItemsHeight`: two lines of New York 10, only when some
registered person item says `showOnCard`; otherwise the ROM's 220 exactly,
so nothing changes for a host without one).  A card with none of the
items' data shows nothing there.

## How an application adds an item

```
myItem := {
    symbol:     '|myThing:MySig|,       // unique: the package's signature in it
    name:       "My thing",             // in the Add picker and the slip's title
    type:       'editor,
    protection: 'public,
    showOnCard: true,                   // host addition: also under the card
    noKeyboard: true,                   // no keyboard in its slip (for a checkbox, a picker)
    _proto:     @8240,                  // Names' edit-slip group of fields
    stepAllocateContext: [],            // see below
    infoFrame: {
        checkPrefix: [true, nil],       // the data is on the card itself (nil, [pathExpr: 'arr] for an array)
        checkPaths:  ['|myThing:MySig|],// the slots that hold it (an item with none filled shows nothing)
        formatFunc:  func(values) ...   // its text - or format: "^0", or shapeFunc
        // actionFunc: func(...) ...    // a tap doing something else than opening the editor
    },
    stepChildren: [ ...the editor's views: read and write target.(slot) ... ]
};
RegCardItem(myItem, nil);               // for 'person, 'company and 'owner (nil); or a list of classes
// UnRegCardItem('|myThing:MySig|, nil) in the package's RemoveScript
```

- **The data** lives in the card's own slots (`target.(slot)`, where
  `target` is the card - the slip and the views inherit it).  Names'
  editors write only the slots they show, so a slot it does not know stays
  on the card and goes with the entry in a backup (a card sent as a vCard
  keeps only what that format holds; not checked: beaming).  Name the slot
  as the symbol, with the package's signature in it.
- **The editor**: Names' own fields are protoLabelInputLines with a `path`
  that `@8240` points at the data (its `stepAllocateContext` lists them,
  field by field, with `addPath`s for array elements).  An editor of its own
  views - a checkbox, a picker - writes the card itself and saves it
  (`EntryChangeXmit`), and **must declare `stepAllocateContext: []`**:
  otherwise `@8240` finds the edit slip's own list by inheritance when the
  slip is opened from a tap and throws (-48204, `addPath`).
- An item whose data is empty is offered by Add; when the editor clears it
  (takes the slot away), Add offers it again.

`RegCardItem(viewDef, classes)` and `UnRegCardItem(symbol, classes)` are the
host's global functions (`host/HostCardItems.ns`, defined at boot on every
host); they only call `RegisterViewDef`/`UnRegisterViewDef` for each class,
so a package that must also run on a MessagePad calls those itself.

## The first item: Do not disturb

`dndCardItem` in `HostQuickSheet.ns` (`docs/host-do-not-disturb.md`): the
card's `hostDNDAllowed`; "Rings during Do not disturb" under the card and
in All Info when set; Add offers "Do not disturb" when it is not; its
editor a checkbox, "Allow calls and texts during Do not disturb", saved as
it is ticked (unticked, the slot is taken away).  It replaced the DND
button the quick sheet had put in Names' status bar (`RegAuxButton`).

ctest: `host.NewtonPhone.<size>` (`src/host/demo/phone.ns`): Bob's card -
Add offering Do not disturb, its editor's checkbox letting him through,
the line drawn under his card, a tap on it opening the editor.

NOT YET: items on the face of a company or owner card are given room only
when a person item asks for it (`CardFaceItemsHeight` looks at `'person`);
a business card shown elsewhere (the In Box, printing) does not show them.
