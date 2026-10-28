# Supported languages

OpenDTU-HEMS supports **German (`de`) and English (`en`)**. Both languages are
built in. Uploading a language pack does not add a language to the WebUI or
device-display selectors. Unsupported language packs are ignored by the firmware
and are no longer distributed in this folder.

WebUI translations live in [webapp/src/locales](../webapp/src/locales/) and the
supplemental message modules in [webapp/src](../webapp/src/). Translation
corrections should update these existing German and English sources.

## Device-display overrides

The existing Configuration > Language Pack restore option remains available for
German or English device-display overrides. At startup, firmware only registers
packs whose `meta.code` is `de` or `en` and whose `meta.name` is nonempty. The
pack's optional `display` entries override the built-in display strings for that
locale; omitted entries keep their built-in defaults.

A pack's `webapp` section does not replace the bundled WebUI translations.
Event/alarm messages also use the built-in German and English firmware strings.
The obsolete Greek, Spanish, Italian and Polish packs have been removed.
