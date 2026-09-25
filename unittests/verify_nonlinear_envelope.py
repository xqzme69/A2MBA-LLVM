import random


def refinement(key, inverse, variant, mask):
    if variant == 0:
        return inverse * (2 - key * inverse) & mask
    if variant == 1:
        return inverse + inverse * (1 - key * inverse) & mask
    return 2 * inverse - key * inverse * inverse & mask


def inverse_for_schedule(key, width, two_bit_seed, schedule):
    mask = (1 << width) - 1
    inverse = (2 - key) & mask if two_bit_seed else key
    correct_bits = 2 if two_bit_seed else 1
    for variant in schedule:
        if correct_bits >= width:
            break
        inverse = refinement(key, inverse, variant, mask)
        correct_bits *= 2
    if correct_bits < width:
        raise AssertionError("incomplete refinement schedule")
    return inverse


def inverse_by_geometric_product(key, width):
    mask = (1 << width) - 1
    error = (1 - key) & mask
    inverse = 1
    bits = 1
    while bits < width:
        inverse = inverse * (1 + error) & mask
        if bits * 2 < width:
            error = error * error & mask
        bits *= 2
    return inverse


def schedules(width, two_bit_seed):
    correct_bits = 2 if two_bit_seed else 1
    rounds = 0
    while correct_bits < width:
        correct_bits *= 2
        rounds += 1
    yield (0,) * rounds
    yield (1,) * rounds
    yield (2,) * rounds
    yield tuple(index % 3 for index in range(rounds))
    yield tuple((index * 2 + 1) % 3 for index in range(rounds))


def verify_key(key, width):
    mask = (1 << width) - 1
    inverse = inverse_by_geometric_product(key, width)
    if key * inverse & mask != 1:
        raise AssertionError(f"bad geometric inverse: width={width}, key={key}")
    for two_bit_seed in (False, True):
        for schedule in schedules(width, two_bit_seed):
            inverse = inverse_for_schedule(key, width, two_bit_seed, schedule)
            if key * inverse & mask != 1:
                raise AssertionError(
                    f"bad inverse: width={width}, key={key}, schedule={schedule}"
                )


def main():
    checks = 0
    for width in (8, 16):
        for key in range(1, 1 << width, 2):
            verify_key(key, width)
            checks += 1

    source = random.Random(0xA2BA5EED)
    for width in (32, 64):
        mask = (1 << width) - 1
        for _ in range(50000):
            product = source.getrandbits(width)
            doubled = product * 2 & mask
            keys = (doubled | 1, doubled + 1 & mask, doubled ^ 1)
            if len(set(keys)) != 1 or not keys[0] & 1:
                raise AssertionError("odd-key constructions diverged")
            verify_key(keys[0], width)
            value = source.getrandbits(width)
            inverse = inverse_for_schedule(
                keys[0],
                width,
                bool(source.getrandbits(1)),
                tuple(index % 3 for index in range(6)),
            )
            decoded = (
                value * keys[0] * inverse & mask,
                value * inverse * keys[0] & mask,
                value * (keys[0] * inverse & mask) & mask,
            )
            if decoded != (value, value, value):
                raise AssertionError("decode order changed the value")
            checks += 1

    print(f"PASS: {checks} nonlinear-envelope keys across 8/16/32/64 bits")


if __name__ == "__main__":
    main()
