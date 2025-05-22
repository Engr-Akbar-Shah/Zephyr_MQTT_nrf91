import os
import re
import sys

# Path setup
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CERTS_DIR = os.path.join(BASE_DIR, "components", "certs")
CERTS_H_PATH = os.path.join(CERTS_DIR, "certs.h")

# Match rules based on filename
MATCH_RULES = {
    "device_cert": ["device", "certificate"],
    "private_key": ["private", "key"],
    "root_ca": ["root"]
}

def match_cert_type(filename):
    name = filename.lower()
    for cert_type, keywords in MATCH_RULES.items():
        if any(keyword in name for keyword in keywords):
            return cert_type
    return None

def convert_cert_to_c_format(file_path, var_name):
    with open(file_path, "r") as f:
        lines = [line.strip() for line in f if line.strip()]
    lines = ['"{}\\n"'.format(line.replace('"', '\\"')) for line in lines]
    return f'static const char {var_name}[] =\n' + '\n'.join(lines) + ';\n\n'

def find_cert_files():
    found = {key: [] for key in MATCH_RULES}
    for fname in os.listdir(CERTS_DIR):
        fpath = os.path.join(CERTS_DIR, fname)
        if not os.path.isfile(fpath):
            continue
        cert_type = match_cert_type(fname)
        if cert_type:
            found[cert_type].append(fpath)
    return found

def main():
    cert_files = find_cert_files()

    # Check for duplicates
    has_duplicates = False
    for key, files in cert_files.items():
        if len(files) > 1:
            print(f"⚠️  Multiple matches for '{key}':")
            for f in files:
                print(f"   - {os.path.basename(f)}")
            has_duplicates = True

    if has_duplicates:
        print("\n❌ Please remove duplicates and re-run the script.")
        sys.exit(1)

    # Generate certificate blocks
    cert_blocks = {}
    for cert_type, files in cert_files.items():
        if files:
            cert_blocks[cert_type] = convert_cert_to_c_format(files[0], cert_type)
        else:
            print(f"⚠️  Warning: No file matched for '{cert_type}'")

    # Generate the header file
    header = "#ifndef _CERTIFICATES_H\n#define _CERTIFICATES_H\n\n"
    header += "int write_device_certs_to_modem(void);\n\n"

    for block in cert_blocks.values():
        header += block

    header += "#endif\n"

    with open(CERTS_H_PATH, "w") as f:
        f.write(header)

    print("✅ certificates.h has been generated successfully.")

if __name__ == "__main__":
    main()
