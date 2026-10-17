# Run inside the Binary Ninja GUI through bnctl exec -f.
import claude_bnx as bnx

view = bv
for address in (0x1401F6640, 0x14014E460, 0x140037C30, 0x140525BF0):
    function = view.get_function_at(address)
    print(f"\nFUNCTION {address:#x}: {function}")
    if function:
        for index, line in enumerate(function.hlil.root.lines):
            if index >= 90:
                print("[function output truncated]")
                break
            print(line)

for address in (0x145013958, 0x144FADCC0, 0x144CCD020):
    print(f"\nDTI REFERENCES {address:#x}")
    references = list(view.get_code_refs(address))
    print(f"count={len(references)}")
    for ref in references[:40]:
        print(f"{ref.address:#x} in {ref.function}")
