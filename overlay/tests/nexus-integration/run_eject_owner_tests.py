"""Run the eject request's production ownership checks with simulated PM, Loader and SD replies."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
commands = (root / "stratosphere/ams_mitm/source/usb_mitm/usb_eject_commands.hpp").read_text()
policy = commands[commands.index("bool UsbLaunchInUse("):commands.index("void ApplyEjectCommand()")]
fixture = Path(__file__).with_name("eject_owner_tests.cpp.in").read_text()
usb = root / "libraries/libstratosphere"
read_only = (usb / "source/usb/usb_filesystem.cpp").read_text()

# Both HOME icon routes must select the lease policy exercised by TestReadLease.
# The by-path route previously retained an archive until HOME closed its cache.
module = (root / "stratosphere/ams_mitm/source/usb_mitm/usbmitm_module.cpp").read_text()
for route in ("OpenUsbControlFileSystemByPath", "OpenUsbControlFileSystem"):
    body = module.split("    Result " + route + "(", 1)[1].split("\n    }", 1)[0]
    assert "usb::filesystem::ReadLease::CachedControl" in body, route
    assert "usb::filesystem::ReadLease::OpenFile" not in body, route
def method_body(signature, next_method):
    start = read_only.index(signature)
    return read_only[read_only.index("{", start):read_only.index(next_method, start)].rstrip()
fixture = fixture.replace("// OPEN_LEASE_BODY", method_body("Result AcquireLease()", "ReadOnlyFile(Service"))
fixture = fixture.replace("// READ_BODY", method_body("virtual Result DoRead(size_t", "virtual Result DoGetSize"))
storage = (usb / "source/usb/usb_storage.cpp").read_text()
start = storage.index("Result Service::RequestEject(")
fixture = fixture.replace("// STORAGE_ADMISSION", storage[start:storage.index("Result Service::ResumeAfterEject", start)])
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory)
    (output / "eject.cpp").write_text(fixture.replace("// EJECT_OWNER_METHODS", policy))
    for optimization in ("-O0", "-O2"):
        subprocess.run(["g++", "-std=c++20", optimization, "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-pthread",
                        "-I" + str(usb/"include/stratosphere/usb"),
                        "-I" + str(usb/"include"),
                        "-I" + str(root/"stratosphere/ams_mitm/source/usb_mitm"),
                        str(output / "eject.cpp"), "-o", str(output / "test")], check=True)
        subprocess.run([str(output / "test")], check=True, timeout=15)
