# Styled combo preview

Run from the repository root with Qt 6's QML runner:

```sh
qml tools/styled_combo_preview/preview.qml
```

Use `qml6` instead if that is your Qt installation's executable name. Qt Quick,
Qt Quick Controls, and Qt Quick Layouts are required. No Briefutil binary,
Proxy backend, PDF renderer, or application build is needed.

All samples instantiate the actual
`briefutil/app/qml/StyledComboBox.qml`; the preview does not copy its delegate,
popup, indicator, or styling. The data and surrounding text field are small
fixtures for the three existing consumer shapes.

Check the sender and profile string lists, the page-preset `ListModel` with its
`textRole`, and the style/language selectors. Open each popup and select with
both pointer and keyboard. The highlighted row, displayed text, and selected
summary should agree. Clear and restore the sender model to check its empty
state. Resize to check text elision and popup alignment.

Switch between light and dark themes while a popup is open and while it is
closed. Use **Ctrl+T** to switch without clicking outside and dismissing the
popup. The triangle should repaint in the current text color. The ordinary
rows retain their 28-pixel height; profile controls retain the adjacent field's
height and their zero background implicit-height override.

Existing profile-save, model-refresh, and selection-restoration code stays in
the three application windows. Those backend interactions still need the
normal application check; this preview exercises the shared control itself.

Qt was unavailable during preparation. Source comparisons, the resource XML,
and the preview's relative import were checked statically; the preview and
its visual checks still need to be run with Qt.
