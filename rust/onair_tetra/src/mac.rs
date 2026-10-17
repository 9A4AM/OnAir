//! Signalling block walk (ETSI EN 300 392-2 clauses 21.4, 21.5 and the MLE of 18): the MAC PDUs of one SCH/HD or SCH/F block. Only broadcasts
//! are read: SYSINFO and the MLE D-NWRK-BROADCAST (neighbour cells, network time). Addresses are skipped and never kept; any other message is only
//! counted by protocol discriminator; air interface encrypted PDUs are only counted. No call, subscriber or short data content is read.
use crate::abi::{MacBlock, Neighbour, Nwrk};
use crate::bits::Reader;
use crate::sysinfo::{parse_sysinfo, Parsed};

/// Address length in bits per MAC-RESOURCE address type (21.5.1).
const ADDR_LEN: [usize; 8] = [0, 24, 10, 24, 24, 34, 30, 34];

/// Caller supplied storage for the broadcasts found in a block.
pub struct Out<'a> {
    pub blk: MacBlock,
    pub nwrk: &'a mut [Nwrk],
    pub nbr: &'a mut [Neighbour],
}

fn parse_neighbour(r: &mut Reader, n: &mut Neighbour) -> bool {
    n.cell_id = r.get(5) as i32;
    n.resel_types = r.get(2) as i32;
    n.synced = r.bit() as i32;
    n.cell_load = r.get(2) as i32;
    n.carrier = r.get(12) as i32;
    if r.bit() {
        // O-bit: the type 2 elements, each behind its P-bit
        if r.bit() {
            n.has_ext = 1;
            n.band = r.get(4) as i32;
            n.offset = r.get(2) as i32;
            n.duplex = r.get(3) as i32;
            n.reverse = r.bit() as i32;
        }
        if r.bit() {
            n.mcc = r.get(10) as i32;
        }
        if r.bit() {
            n.mnc = r.get(14) as i32;
        }
        if r.bit() {
            n.la = r.get(14) as i32;
        }
        if r.bit() {
            n.max_tx = r.get(3) as i32;
        }
        if r.bit() {
            n.min_rx = r.get(4) as i32;
        }
        if r.bit() {
            n.subscriber_class = r.get(16) as i32;
        }
        if r.bit() {
            n.bs_service = r.get(12) as i32;
        }
        if r.bit() {
            n.timeshare = r.get(5) as i32;
        }
        if r.bit() {
            n.frame_offset = r.get(6) as i32;
        }
    }
    r.ok
}

/// D-NWRK-BROADCAST (18.4.2.x) after the PDU type; appended to `out` only when the whole element parsed.
fn parse_nwrk(r: &mut Reader, out: &mut Out) {
    let mut n = Nwrk { reselect: r.get(16) as i32, cell_load: r.get(2) as i32, ..Nwrk::default() };
    let mut count = 0;
    if r.left() > 0 && r.bit() {
        if r.bit() {
            n.has_time = 1;
            n.time48 = r.get(48);
        }
        if r.bit() {
            count = r.get(3) as usize;
        }
    }
    let first = out.blk.n_nbr as usize;
    let mut got = 0usize;
    let mut i = 0;
    while i < count && r.ok {
        let mut nb = Neighbour::default();
        if parse_neighbour(r, &mut nb) {
            if let Some(slot) = out.nbr.get_mut(first + got) {
                *slot = nb;
            }
            got += 1;
        }
        i += 1;
    }
    if r.ok {
        n.nbr_first = first as u32;
        n.nbr_count = got as u32;
        let k = out.blk.n_nwrk as usize;
        if let Some(slot) = out.nwrk.get_mut(k) {
            *slot = n;
        }
        out.blk.n_nwrk += 1;
        out.blk.n_nbr += got as u32;
    }
}

/// TL-SDU: the LLC header, then the MLE; only the mobility link entity's (protocol discriminator 5) broadcast is read.
fn parse_tl_sdu(b: &[u8], from: usize, to: usize, out: &mut Out) {
    let mut r = Reader::new(b, from, to);
    let llc = r.get(4);
    match llc {
        0 | 4 => {
            r.get(2);
        } // BL-ADATA: N(R), N(S)
        1 | 5 => {
            r.get(1);
        } // BL-DATA: N(S)
        2 | 6 => {} // BL-UDATA
        3 | 7 => {
            r.get(1);
        } // BL-ACK: N(R)
        _ => return, // AL- (advanced link) and others: not read
    }
    if llc >= 4 {
        if r.end < r.p + 32 {
            return;
        }
        r.end -= 32; // the frame check sequence
    }
    let pd = r.get(3) as usize;
    if !r.ok {
        return;
    }
    out.blk.by_pd[pd & 7] += 1;
    if pd != 5 {
        return;
    }
    let ty = r.get(3);
    if ty == 2 {
        parse_nwrk(&mut r, out);
    }
}

/// The channel allocation element (21.5.2): its length, or -1 when it cannot be skipped (augmented).
fn chan_alloc_len(b: &[u8], p: usize) -> i64 {
    let mut r = Reader::new(b, p, b.len());
    r.get(2);
    r.get(4);
    let ul_dl = r.get(2);
    r.get(1);
    r.get(1);
    r.get(12);
    if r.bit() {
        r.get(10);
    }
    if r.get(2) == 0 {
        r.get(2);
    }
    if ul_dl == 0 {
        return -1;
    }
    if r.ok {
        (r.p - p) as i64
    } else {
        -1
    }
}

/// Walk one type 1 signalling block (the bits of an SCH/HD or SCH/F, one per byte). Counters and the SYSINFO go to `out.blk`, broadcasts to the buffers.
pub fn parse_block(b: &[u8], out: &mut Out) {
    let n = b.len();
    let mut pos = 0usize;
    while pos + 16 <= n {
        let mut r = Reader::new(b, pos, n);
        let ty = r.get(2);
        if ty == 2 {
            // broadcast: SYSINFO or ACCESS-DEFINE
            let bt = r.get(2);
            if bt == 0 && n - pos >= 124 {
                let mut si = Default::default();
                if parse_sysinfo(&b[pos..pos + 124], &mut si) == Parsed::Ok {
                    out.blk.sysinfo = 1;
                    out.blk.si = si;
                }
            } else if bt == 1 {
                out.blk.access_define = 1;
            }
            break;
        }
        if ty == 1 {
            out.blk.fragments += 1; // MAC-FRAG / MAC-END: a continuation, not reassembled
            break;
        }
        if ty == 3 {
            break; // supplementary / U-signal
        }
        let fill = r.bit();
        r.bit(); // position of grant
        let enc = r.get(2);
        r.bit(); // random access flag
        let li = r.get(6) as usize;
        let at = r.get(3) as usize;
        if at == 0 {
            break; // null PDU: the rest of the block is empty
        }
        let mut frag = false;
        let mut len;
        if li == 0x3F || li == 0x3E {
            frag = li == 0x3F;
            len = n - pos;
        } else if (1..=0x3A).contains(&li) {
            len = li * 8;
        } else {
            break;
        }
        if pos + len > n {
            len = n - pos;
        }
        out.blk.resources += 1;
        r.get(ADDR_LEN[at & 7]); // the address is skipped, never kept
        if r.bit() {
            r.get(4); // power control
        }
        if r.bit() {
            r.get(8); // slot granting
        }
        let ca = r.bit();
        if enc != 0 {
            out.blk.encrypted += 1;
        } else if r.ok {
            let mut readable = true;
            if ca {
                let l = chan_alloc_len(b, r.p);
                if l < 0 {
                    readable = false;
                } else {
                    r.p += l as usize;
                }
            }
            if readable && !frag {
                let mut end = pos + len;
                if fill {
                    while end > r.p && b[end - 1] == 0 {
                        end -= 1;
                    }
                    if end > r.p {
                        end -= 1; // fill bits: a 1 then 0s
                    }
                }
                if end > r.p {
                    parse_tl_sdu(b, r.p, end, out);
                }
            }
        }
        if frag {
            out.blk.fragments += 1;
            break;
        }
        pos += len;
    }
}
