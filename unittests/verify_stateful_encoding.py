import random


def run_width(width):
    modulus = 1 << width
    mask = modulus - 1
    random_source = random.Random(0xA2_57_41 + width)

    for _ in range(50_000):
        state = random_source.getrandbits(width)
        value = random_source.getrandbits(width)
        left = random_source.getrandbits(width)
        right = random_source.getrandbits(width)
        context = random_source.getrandbits(width)
        state_salt = random_source.getrandbits(width)
        first_salt = random_source.getrandbits(width)
        second_salt = random_source.getrandbits(width)

        next_state = ((state ^ value) * ((state ^ context) | 1) + state_salt) & mask
        bias = ((next_state + first_salt) * (left ^ second_salt)) & mask
        key_product = ((next_state ^ second_salt) * (right + first_salt)) & mask
        key = ((key_product << 1) | 1) & mask
        inverse = pow(key, -1, modulus)
        encoded = ((value + bias) * key) & mask
        decoded = (encoded * inverse - bias) & mask

        if decoded != value:
            raise SystemExit(
                f"stateful encoding failed at i{width}: "
                f"value={value:#x}, decoded={decoded:#x}"
            )


def main():
    for width in (32, 64):
        run_width(width)
    print("PASS: 100000 stateful encoding checks across 32/64 bits")


if __name__ == "__main__":
    main()
