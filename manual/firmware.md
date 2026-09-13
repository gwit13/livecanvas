What code, of what sizes are we actually uploading?

```
python -m esp_idf_size --format tree .pio/build/blink/firmware.map
```

Read the instructions:
```
export TC=~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf
$TC-objdump -d -S -C --disassemble='setup()' --disassemble='loop()' \
  .pio/build/blink/firmware.elf
```