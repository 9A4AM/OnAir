use crate::abi::{MacBlock, Neighbour, Nwrk, Sync, Sysinfo};
use crate::mac::{parse_block, Out};
use crate::sysinfo::*;

struct W(Vec<u8>);
impl W {
    fn put(&mut self, v: u64, n: usize) {
        for i in (0..n).rev() {
            self.0.push(((v >> i) & 1) as u8);
        }
    }
}

fn sync_bits() -> Vec<u8> {
    let mut w = W(vec![]);
    // system code 1, colour 33, tn 3 (field 2), fn 7, mn 12, sharing 1, reserved 2, dtx 1, ext 0, spare 0, mcc 262, mnc 1001, nbr 3, load 2, late 1
    for (v, n) in
        [(1, 4), (33, 6), (2, 2), (7, 5), (12, 6), (1, 2), (2, 3), (1, 1), (0, 1), (0, 1), (262, 10), (1001, 14), (3, 2), (2, 2), (1, 1)]
    {
        w.put(v, n);
    }
    w.0
}

#[test]
fn sync_known() {
    let mut s = Sync::default();
    assert_eq!(parse_sync(&sync_bits(), &mut s), Parsed::Ok);
    assert_eq!((s.colour_code, s.tn, s.fn_, s.mn, s.mcc, s.mnc, s.late_entry), (33, 3, 7, 12, 262, 1001, 1));
    assert_eq!(parse_sync(&sync_bits()[..30], &mut s), Parsed::Partial);
}

fn sysinfo_bits() -> Vec<u8> {
    let mut w = W(vec![]);
    for (v, n) in [
        (2, 2),
        (0, 2),
        (1000, 12),
        (4, 4),
        (1, 2),
        (1, 3),
        (0, 1),
        (0, 2),
        (3, 3),
        (5, 4),
        (6, 4),
        (7, 4),
        (0, 1),
        (0x1234, 16),
        (3, 2),
        (0x8F000, 20),
        (77, 14),
        (0xFFFF, 16),
        (0x0A0, 12),
    ] {
        w.put(v, n);
    }
    w.0
}

#[test]
fn sysinfo_known() {
    let b = sysinfo_bits();
    assert_eq!(b.len(), 124);
    let mut s = Sysinfo::default();
    assert_eq!(parse_sysinfo(&b, &mut s), Parsed::Ok);
    assert_eq!((s.main_carrier, s.band, s.la, s.bs_service), (1000, 4, 77, 0x0A0));
    assert_eq!(security(&s), 1 | 1 << 1);
    assert_eq!(dl_carrier_hz(4, 1000, 1), 4.0 * 100e6 + 25e6 + 6250.0);
    let mut o = [0u8; 256];
    let n = service_text(0x0A0, &mut o);
    assert_eq!(&o[..n], b"migration, voice".as_slice());
    let mut x = Sysinfo::default();
    assert_eq!(parse_sysinfo(&[0, 0, 0, 0], &mut x), Parsed::Rejected);
}

#[test]
fn time_known() {
    // 2025-01-01 00:00:10 UTC, local +01:00 (offs 4 x 15 min)
    let utc2 = 5u64;
    let t = utc2 << 24 | 4 << 17 | 25 << 11;
    let mut o = [0u8; 96];
    let n = network_time_text(t, &mut o);
    assert_eq!(&o[..n], b"2025-01-01 00:00:10 UTC, local +01:00");
    assert_eq!(network_time_text(0xFF_FFFF << 24, &mut o), 0);
}

#[test]
fn usage() {
    assert_eq!(aach_dl_usage(0, false), 2);
    assert_eq!(aach_dl_usage(1 << 12 | 5 << 6, false), 5);
    assert_eq!(aach_dl_usage(0, true), -1);
}

#[test]
fn block_sysinfo_and_garbage() {
    let mut b = sysinfo_bits();
    b.resize(124, 0);
    let mut nw = [Nwrk::default(); 4];
    let mut nb = [Neighbour::default(); 8];
    let mut o = Out { blk: MacBlock::default(), nwrk: &mut nw, nbr: &mut nb };
    parse_block(&b, &mut o);
    assert_eq!(o.blk.sysinfo, 1);
    // any bytes must not panic
    let mut x = 12345u32;
    for len in 0..300 {
        let v: Vec<u8> = (0..len)
            .map(|_| {
                x = x.wrapping_mul(1664525).wrapping_add(1013904223);
                ((x >> 20) & 1) as u8
            })
            .collect();
        let mut o = Out { blk: MacBlock::default(), nwrk: &mut nw, nbr: &mut nb };
        parse_block(&v, &mut o);
    }
}
