# Run inside Binary Ninja through bnctl exec -f.
for function in bv.functions:
    if not 0x141671F00 <= function.start < 0x141672B00:
        continue
    text = str(function.hlil)
    print(f"\n{function.start:#x} {function} [{len(text)} characters]")
    if len(text) <= 900:
        print(text)
    else:
        print("[body omitted: inspect this function separately]")
