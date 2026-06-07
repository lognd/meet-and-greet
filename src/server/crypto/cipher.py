"""XOR cipher for student IDs.

The key is a 32-char hex string (16 bytes). The student ID is treated as its
decimal string representation, XOR'd with repeated key bytes, then hex-encoded.
Decryption reverses: hex-decode, XOR, parse decimal string as int.
"""


def _key_bytes(key_hex: str) -> bytes:
    return bytes.fromhex(key_hex)


def encrypt_id(student_id: int, key_hex: str) -> str:
    raw = str(student_id).encode()
    key = _key_bytes(key_hex)
    xored = bytes(b ^ key[i % len(key)] for i, b in enumerate(raw))
    return xored.hex()


def decrypt_id(encrypted_hex: str, key_hex: str) -> int:
    key = _key_bytes(key_hex)
    xored = bytes.fromhex(encrypted_hex)
    raw = bytes(b ^ key[i % len(key)] for i, b in enumerate(xored))
    return int(raw.decode())
