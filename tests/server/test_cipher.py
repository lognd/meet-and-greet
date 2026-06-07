from server.crypto.cipher import encrypt_id, decrypt_id

KEY     = "deadbeefcafebabedeadbeefcafebabe"
ALT_KEY = "aabbccddaabbccddaabbccddaabbccdd"


def test_roundtrip_simple():
    assert decrypt_id(encrypt_id(12345678, KEY), KEY) == 12345678

def test_roundtrip_zero():
    assert decrypt_id(encrypt_id(0, KEY), KEY) == 0

def test_roundtrip_large():
    n = 999_999_999_999
    assert decrypt_id(encrypt_id(n, KEY), KEY) == n

def test_output_is_hex():
    enc = encrypt_id(42, KEY)
    assert all(c in "0123456789abcdef" for c in enc)

def test_different_ids_differ():
    assert encrypt_id(1, KEY) != encrypt_id(2, KEY)

def test_different_keys_differ():
    assert encrypt_id(100, KEY) != encrypt_id(100, ALT_KEY)

def test_wrong_key_gives_wrong_plaintext():
    enc = encrypt_id(55, KEY)
    try:
        result = decrypt_id(enc, ALT_KEY)
        assert result != 55   # decrypted to a different number
    except (ValueError, UnicodeDecodeError):
        pass  # XOR with wrong key produced non-numeric bytes - also correct

def test_length_is_twice_digit_count():
    for n in [1, 10, 999, 1_000_000]:
        enc = encrypt_id(n, KEY)
        assert len(enc) == len(str(n)) * 2
