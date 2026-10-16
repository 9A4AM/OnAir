//! AIS bit fields. Bits are one per byte in transmission order; only bit 0 of each byte counts (as in the C++ code).

/// `len` bits from `pos`, most significant first; bits past the end read as 0. Fields wider than 32 bits keep the last 32.
pub fn get_u(b: &[u8], pos: usize, len: usize) -> u32 {
    let mut v: u32 = 0;
    for i in 0..len {
        let bit = match pos.checked_add(i).and_then(|p| b.get(p)) {
            Some(x) => (*x & 1) as u32,
            None => 0,
        };
        v = (v << 1) | bit;
    }
    v
}

/// Two's complement field.
pub fn get_s(b: &[u8], pos: usize, len: usize) -> i32 {
    let mut v = get_u(b, pos, len);
    if len > 0 && len < 32 && (v >> (len - 1)) & 1 != 0 {
        v |= !0u32 << len;
    }
    v as i32
}

/// 6-bit text into `out`; trailing '@' and spaces removed. Returns the length (at most `len / 6`, and at most `out.len()`).
pub fn get_text(b: &[u8], pos: usize, len: usize, out: &mut [u8]) -> usize {
    let mut n = 0;
    let mut i = 0;
    while i + 6 <= len && n < out.len() {
        let v = get_u(b, pos.wrapping_add(i), 6);
        out[n] = if v < 32 { (v + 64) as u8 } else { v as u8 };
        n += 1;
        i += 6;
    }
    while n > 0 && (out[n - 1] == b'@' || out[n - 1] == b' ') {
        n -= 1;
    }
    n
}
