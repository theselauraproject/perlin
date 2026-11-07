"""Read-only export, run in the selected BDS IDA Python context.

Call export_reference() and save its JSON result outside the source tree (build/).
No BDS code bytes are checked into the repository.
"""

import hashlib
import json

import ida_bytes
import ida_funcs
import ida_nalt
import idautils


def export_reference():
    def read_page(address):
        data = ida_bytes.get_bytes(address, 4096)
        if data is None:
            data = bytes(ida_bytes.get_byte(ea) if ida_bytes.is_loaded(ea) else 0
                         for ea in range(address, address + 4096))
        return data.hex()

    functions = {}
    for address in (0x140EB2C20, 0x14103D9D0, 0x140EB0DA0,
                    0x1454F9850, 0x146251EA0, 0x1462527E0, 0x140EAFAF0,
                    0x140E104A0, 0x140E105A0, 0x146254380,
                    0x144795D10, 0x14103BE00, 0x140BBD770, 0x146250370,
                    0x140BBD140, 0x140BBA5C0,
                    0x140C1C8E0, 0x1403616A0, 0x1435AAF50, 0x140C2C180,
                    0x1454FAE40, 0x146251860, 0x146251B16, 0x146250390,
                    0x140F90EA0, 0x140F90C90,
                    0x140C1C5D0, 0x140BEA420, 0x1403420E0, 0x14030D2C0,
                    0x14103E8F0, 0x14104F970, 0x14104FC50, 0x141051E00,
                    0x141051C40, 0x14104F680, 0x14104A290, 0x140341710,
                    0x14103D110, 0x143972690, 0x140F1E690,
                    0x1410114B0, 0x140B6FD10, 0x14387A590,
                    0x143261390, 0x146254354, 0x140C66F70, 0x140F4FB60,
                    0x1432674C0, 0x146251E30, 0x146251E10, 0x146253D80,
                    0x140C638A0, 0x140C70450, 0x140C706A0, 0x140C70600,
                    0x140C77F80, 0x140C69760, 0x140BA9900, 0x140C70850,
                    0x140C78040, 0x140BECF40,
                    0x140F1D820, 0x143F0ECA0, 0x144282EC0, 0x143F3BF60,
                    0x140F23000, 0x140F22ED0, 0x140F22DD0, 0x140F22C80,
                    0x140F1D190, 0x14031FD50, 0x140C1BA90,
                    0x141052BF0, 0x14478D6B0, 0x143EA1990,
                    0x143E9FFF0, 0x143EA1C60, 0x143F386B0,
                    0x14048C340, 0x1441FC5A0, 0x1441FC8B0, 0x140F924E0,
                    0x140F9DAF0, 0x140355790, 0x140C1EF20,
                    0x140B58EA0, 0x140B80D80, 0x140C1EF10,
                    0x140C1EE80, 0x140C1EDC0, 0x140E76AD0, 0x140E76C70,
                    0x1410EE9B0, 0x143E8D580, 0x140EAFFD0, 0x140EC4780,
                    0x140EAF890, 0x140DEFBF0, 0x140B58CF0,
                    0x140E0E820, 0x140E0E6F0, 0x140FC98E0,
                    0x140FC94C0, 0x140FC8FA0, 0x1447976A0):
        function = ida_funcs.get_func(address)
        if function is None or function.start_ea != address:
            raise ValueError(f"Missing function at {address:#x}")
        chunks = list(idautils.Chunks(address))
        pages = {}
        code_hash = hashlib.sha256()
        for start, end in chunks:
            code_hash.update(ida_bytes.get_bytes(start, end - start))
            for page in range(start & ~4095, (end + 4095) & ~4095, 4096):
                pages[hex(page)] = read_page(page)
            for instruction in idautils.Heads(start, end):
                for target in idautils.DataRefsFrom(instruction):
                    page = target & ~4095
                    if ida_bytes.is_loaded(target):
                        pages[hex(page)] = read_page(page)
        if address == 0x1441FC8B0:
            pages['0x147291000'] = read_page(0x147291000)
        if address == 0x140355790:
            pages['0x14740e000'] = read_page(0x14740E000)
        if address in (0x140EAFAF0, 0x140EAFFD0):
            pages['0x147337000'] = read_page(0x147337000)
            pages['0x147147000'] = read_page(0x147147000)
        functions[hex(address)] = {
            "chunks": chunks,
            "code_sha256": code_hash.hexdigest(),
            "pages": pages,
        }
    return json.dumps({
        "input_name": ida_nalt.get_root_filename(),
        "input_sha256": ida_nalt.retrieve_input_file_sha256().hex(),
        "functions": functions,
    })
