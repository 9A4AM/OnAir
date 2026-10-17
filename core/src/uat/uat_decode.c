// Part of dump978, a UAT decoder.
//
// Copyright 2015, Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you may copy, redistribute and/or modify it  
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your  
// option) any later version.  
//
// This file is distributed in the hope that it will be useful, but  
// WITHOUT ANY WARRANTY; without even the implied warranty of  
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License  
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <math.h>
#include <string.h>
#include <assert.h>

#include "dect2/uat/uat_frame.h"
#include "dect2/uat/uat_decode_c.h"


static const char *address_qualifier_names[8] = {
    "ICAO address via ADS-B",
    "reserved (national use)",
    "ICAO address via TIS-B",
    "TIS-B track file address",
    "Vehicle address",
    "Fixed ADS-B Beacon Address",
    "reserved (6)",
    "reserved (7)"
};    

static void uat_display_hdr(const struct uat_adsb_mdb *mdb, FILE *to)
{
    fprintf(to,
            "HDR:\n"
            " MDB Type:          %d\n"
            " Address:           %06X (%s)\n",
            mdb->mdb_type, 
            mdb->address,
            address_qualifier_names[mdb->address_qualifier]);
}


static void uat_display_sv(const struct uat_adsb_mdb *mdb, FILE *to)
{
    if (!mdb->has_sv)
        return;

    fprintf(to,
            "SV:\n"
            " NIC:               %u\n",
            mdb->nic);

    if (mdb->position_valid)
        fprintf(to,
                " Latitude:          %+.4f\n"
                " Longitude:         %+.4f\n",
                mdb->lat,
                mdb->lon);

    switch (mdb->altitude_type) {
    case ALT_BARO:
        fprintf(to,
                " Altitude:          %d ft (barometric)\n",
                mdb->altitude);
        break;
    case ALT_GEO:
        fprintf(to,
                " Altitude:          %d ft (geometric)\n",
                mdb->altitude);
        break;
    default:
        break;
    }

    if (mdb->ns_vel_valid)
        fprintf(to,
                " N/S velocity:      %d kt\n",
                mdb->ns_vel);

    if (mdb->ew_vel_valid)
        fprintf(to,
                " E/W velocity:      %d kt\n",
                mdb->ew_vel);

    switch (mdb->track_type) {
    case TT_TRACK:
        fprintf(to,
                " Track:             %u\n",
                mdb->track);
        break;
    case TT_MAG_HEADING:
        fprintf(to,
                " Heading:           %u (magnetic)\n",
                mdb->track);
        break;
    case TT_TRUE_HEADING:
        fprintf(to,
                " Heading:           %u (true)\n",
                mdb->track);
        break;
    default:
        break;
    }

    if (mdb->speed_valid)
        fprintf(to,
                " Speed:             %u kt\n",
                mdb->speed);
        

    switch (mdb->vert_rate_source) {
    case ALT_BARO:
        fprintf(to,
                " Vertical rate:     %d ft/min (from barometric altitude)\n",
                mdb->vert_rate);
        break;
    case ALT_GEO:
        fprintf(to,
                " Vertical rate:     %d ft/min (from geometric altitude)\n",
                mdb->vert_rate);
        break;
    default:
        break;
    }
        
    if (mdb->dimensions_valid)
        fprintf(to,
                " Dimensions:        %.1fm L x %.1fm W%s\n",
                mdb->length, mdb->width,
                mdb->position_offset ? " (position offset applied)" : "");
    
    fprintf(to,
            " UTC coupling:      %s\n"
            " TIS-B site ID:     %u\n",
            mdb->utc_coupled ? "yes" : "no",
            mdb->tisb_site_id);
}


static const char *emitter_category_names[40] = {
    "No information",                          // A0
    "Light <= 7000kg",
    "Medium Wake 7000-34000kg",
    "Medium Wake 34000-136000kg",
    "Medium Wake High Vortex 34000-136000kg",
    "Heavy >= 136000kg",
    "Highly Maneuverable",
    "Rotorcraft",                              // A7
    "reserved (8)",                            // B0
    "Glider/Sailplane",
    "Lighter than air",
    "Parachutist / sky diver",
    "Ultra light / hang glider / paraglider",
    "reserved (13)",
    "UAV",
    "Space / transatmospheric",                // B7
    "reserved (16)",                           // C0
    "Emergency vehicle",
    "Service vehicle",
    "Point obstacle",
    "Cluster obstacle",
    "Line obstacle",
    "reserved (22)",
    "reserved (23)",                           // C7
    "reserved (24)",
    "reserved (25)",
    "reserved (26)",
    "reserved (27)",
    "reserved (28)",
    "reserved (29)",
    "reserved (30)",
    "reserved (31)",
    "reserved (32)",
    "reserved (33)",
    "reserved (34)",
    "reserved (35)",
    "reserved (36)",
    "reserved (37)",
    "reserved (38)",
    "reserved (39)"
};    

static const char *emergency_status_names[8] = {
    "No emergency",
    "General emergency",
    "Lifeguard / Medical emergency",
    "Minimum fuel",
    "No communications",
    "Unlawful interference",
    "Downed aircraft",
    "reserved"
};

static void uat_display_ms(const struct uat_adsb_mdb *mdb, FILE *to)
{
    if (!mdb->has_ms)
        return;

    fprintf(to,
            "MS:\n"
            " Emitter category:  %s\n"
            " Callsign:          %s%s\n"
            " Emergency status:  %s\n"
            " UAT version:       %u\n"
            " SIL:               %u\n"
            " Transmit MSO:      %u\n"
            " NACp:              %u\n"
            " NACv:              %u\n"
            " NICbaro:           %u\n"
            " Capabilities:      %s%s\n"
            " Active modes:      %s%s%s\n"
            " Target track type: %s\n",
            emitter_category_names[mdb->emitter_category],
            mdb->callsign_type == CS_SQUAWK ? "squawk " : "",
            mdb->callsign_type == CS_INVALID ? "unavailable" : mdb->callsign,
            emergency_status_names[mdb->emergency_status],
            mdb->uat_version,
            mdb->sil,
            mdb->transmit_mso,
            mdb->nac_p,
            mdb->nac_v,
            mdb->nic_baro,
            mdb->has_cdti ? "CDTI " : "", mdb->has_acas ? "ACAS " : "",
            mdb->acas_ra_active ? "ACASRA " : "", mdb->ident_active ? "IDENT " : "", mdb->atc_services ? "ATC " : "",
            mdb->heading_type == HT_MAGNETIC ? "magnetic heading" : "true heading");
}



static void uat_display_auxsv(const struct uat_adsb_mdb *mdb, FILE *to)
{
    if (!mdb->has_auxsv)
        return;

    fprintf(to,
            "AUXSV:\n");

    switch (mdb->sec_altitude_type) {
    case ALT_BARO:
        fprintf(to,
                " Sec. altitude:     %d ft (barometric)\n",
                mdb->sec_altitude);
        break;
    case ALT_GEO:
        fprintf(to,
                " Sec. altitude:     %d ft (geometric)\n",
                mdb->sec_altitude);
        break;        
    default:
        fprintf(to,
                " Sec. altitude:     unavailable\n");
        break;
    }
}


void uat_display_adsb_mdb(const struct uat_adsb_mdb *mdb, FILE *to)
{
    uat_display_hdr(mdb, to);
    uat_display_sv(mdb, to);
    uat_display_ms(mdb, to);
    uat_display_auxsv(mdb, to);
}



static void display_generic_data(uint8_t *data, uint16_t length, FILE *to)
{
    unsigned i;

    fprintf(to,
            " Data:              ");
    for (i = 0; i < length; i += 16) {
        unsigned j;
        
        if (i > 0)
            fprintf(to,
                    "                    ");
        
        for (j = i; j < i+16; ++j) {
            if (j < length)
                fprintf(to, "%02X ", data[j]);
            else
                fprintf(to, "   ");
        }
        
        for (j = i; j < i+16 && j < length; ++j) {
            fprintf(to, "%c", 
                    (data[j] >= 32 && data[j] < 127) ? data[j] : '.');
        }
        fprintf(to, "\n");
    }
}

// The odd two-string-literals here is to avoid \0x3ABCDEF being interpreted as a single (very large valued) character
static const char *dlac_alphabet = "\x03" "ABCDEFGHIJKLMNOPQRSTUVWXYZ\x1A\t\x1E\n| !\"#$%&'()*+,-./0123456789:;<=>?";

static const char *decode_dlac(uint8_t *data, unsigned bytelen)
{
    static char buf[1024];
    uint8_t *end = data + bytelen;
    char *p = buf;
    int step = 0;
    int tab = 0;
    
    while (data < end) {
        int ch;

        assert(step >= 0 && step <= 3);
        switch (step) {
        case 0:
            ch = data[0] >> 2;
            ++data;
            break;
        case 1:
            ch = ((data[-1] & 0x03) << 4) | (data[0] >> 4);
            ++data;
            break;
        case 2:
            ch = ((data[-1] & 0x0f) << 2) | (data[0] >> 6);
            break;
        case 3:
            ch = data[0] & 0x3f;
            ++data;
            break;
        }

        if (tab) {
            while (ch > 0)
                *p++ = ' ', ch--;
            tab = 0;
        } else if (ch == 28) { // tab
            tab = 1;
        } else {
            *p++ = dlac_alphabet[ch];
        }

        step = (step+1)%4;
    }

    *p = 0;
    return buf;
}
    
static const char *get_fisb_product_name(uint16_t product_id)
{
    switch (product_id) {
    case 0: case 20: return "METAR and SPECI";
    case 1: case 21: return "TAF and Amended TAF";
    case 2: case 22: return "SIGMET";
    case 3: case 23: return "Convective SIGMET";
    case 4: case 24: return "AIRMET";
    case 5: case 25: return "PIREP";
    case 6: case 26: return "AWW";
    case 7: case 27: return "Winds and Temperatures Aloft";
    case 8: return "NOTAM (Including TFRs) and Service Status";
    case 9: return "Aerodrome and Airspace – D-ATIS";
    case 10: return "Aerodrome and Airspace - TWIP";
    case 11: return "Aerodrome and Airspace - AIRMET";
    case 12: return "Aerodrome and Airspace - SIGMET/Convective SIGMET";
    case 13: return "Aerodrome and Airspace - SUA Status";
    case 51: return "National NEXRAD, Type 0 - 4 level";
    case 52: return "National NEXRAD, Type 1 - 8 level (quasi 6-level VIP)";
    case 53: return "National NEXRAD, Type 2 - 8 level";
    case 54: return "National NEXRAD, Type 3 - 16 level";
    case 55: return "Regional NEXRAD, Type 0 - low dynamic range";
    case 56: return "Regional NEXRAD, Type 1 - 8 level (quasi 6-level VIP)";
    case 57: return "Regional NEXRAD, Type 2 - 8 level";
    case 58: return "Regional NEXRAD, Type 3 - 16 level";
    case 59: return "Individual NEXRAD, Type 0 - low dynamic range";
    case 60: return "Individual NEXRAD, Type 1 - 8 level (quasi 6-level VIP)";
    case 61: return "Individual NEXRAD, Type 2 - 8 level";
    case 62: return "Individual NEXRAD, Type 3 - 16 level";
    case 63: return "Global Block Representation - Regional NEXRAD, Type 4 – 8 level";
    case 64: return "Global Block Representation - CONUS NEXRAD, Type 4 - 8 level";
    case 81: return "Radar echo tops graphic, scheme 1: 16-level";
    case 82: return "Radar echo tops graphic, scheme 2: 8-level";
    case 83: return "Storm tops and velocity";
    case 101: return "Lightning strike type 1 (pixel level)";
    case 102: return "Lightning strike type 2 (grid element level)";
    case 151: return "Point phenomena, vector format";
    case 201: return "Surface conditions/winter precipitation graphic";
    case 202: return "Surface weather systems";
    case 254: return "AIRMET, SIGMET: Bitmap encoding";
    case 351: return "System Time";
    case 352: return "Operational Status";
    case 353: return "Ground Station Status";
    case 401: return "Generic Raster Scan Data Product APDU Payload Format Type 1";
    case 402: case 411: return "Generic Textual Data Product APDU Payload Format Type 1";
    case 403: return "Generic Vector Data Product APDU Payload Format Type 1";
    case 404: case 412: return "Generic Symbolic Product APDU Payload Format Type 1";
    case 405: case 413: return "Generic Textual Data Product APDU Payload Format Type 2";
    case 600: return "FISDL Products – Proprietary Encoding";
    case 2000: return "FAA/FIS-B Product 1 – Developmental";
    case 2001: return "FAA/FIS-B Product 2 – Developmental";
    case 2002: return "FAA/FIS-B Product 3 – Developmental";
    case 2003: return "FAA/FIS-B Product 4 – Developmental";
    case 2004: return "WSI Products - Proprietary Encoding";
    case 2005: return "WSI Developmental Products";
    default: return "unknown";
    }
}

static const char *get_fisb_product_format(uint16_t product_id)
{
    switch (product_id) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: 
    case 351: case 352: case 353:
    case 402: case 405:
        return "Text";

    case 8: case 9: case 10: case 11: case 12: case 13:        
        return "Text/Graphic";
       
    case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: 
    case 411: case 413:
        return "Text (DLAC)";

    case 51: case 52: case 53: case 54: case 55: case 56: case 57: case 58:
    case 59: case 60: case 61: case 62: case 63: case 64:
    case 81: case 82: case 83: 
    case 101: case 102:
    case 151:
    case 201: case 202:
    case 254:
    case 401:
    case 403:
    case 404:
        return "Graphic";

    case 412:
        return "Graphic (DLAC)";

    case 600: case 2004:
        return "Proprietary";

    case 2000: case 2001: case 2002: case 2003: case 2005: 
        return "Developmental";

    default:
        return "unknown";
    }
}

static void uat_display_fisb_frame(const struct fisb_apdu *apdu, FILE *to)
{
    fprintf(to, 
            "FIS-B:\n"
            " Flags:             %s%s%s%s\n"
            " Product ID:        %u (%s) - %s\n",
            apdu->a_flag ? "A" : "",
            apdu->g_flag ? "G" : "",
            apdu->p_flag ? "P" : "",
            apdu->s_flag ? "S" : "",
            apdu->product_id,
            get_fisb_product_name(apdu->product_id),
            get_fisb_product_format(apdu->product_id));

    fprintf(to,
            " Product time:      ");
    if (apdu->monthday_valid)
        fprintf(to, "%u/%u ", apdu->month, apdu->day);
    fprintf(to, "%02u:%02u", apdu->hours, apdu->minutes);
    if (apdu->seconds_valid)
        fprintf(to, ":%02u", apdu->seconds);
    fprintf(to, "\n");

    switch (apdu->product_id) {
    case 413:
        {
            // Generic text, DLAC
            const char *text = decode_dlac(apdu->data, apdu->length);
            const char *report = text;
            
            while (report) {
                char report_buf[1024];
                const char *next_report;
                char *p, *r;
                
                next_report = strchr(report, '\x1e'); // RS
                if (!next_report)
                    next_report = strchr(report, '\x03'); // ETX
                if (next_report) {
                    memcpy(report_buf, report, next_report - report);
                    report_buf[next_report - report] = 0;
                    report = next_report + 1;
                } else {
                    strcpy(report_buf, report);
                    report = NULL;
                }
                
                if (!report_buf[0])
                    continue;

                r = report_buf;
                p = strchr(r, ' ');
                if (p) {
                    *p = 0;
                    fprintf(to,
                            " Report type:       %s\n",
                            r);
                    r = p+1;
                }
                
                p = strchr(r, ' ');
                if (p) {
                    *p = 0;
                    fprintf(to,
                            " Report location:   %s\n",
                            r);
                    r = p+1;
                }
                
                p = strchr(r, ' ');
                if (p) {
                    *p = 0;
                    fprintf(to,
                            " Report time:       %s\n",
                            r);
                    r = p+1;
                }
                
                fprintf(to,
                        " Text:\n%s\n",
                        r);
            }
        }            
        break;
    default:
        display_generic_data(apdu->data, apdu->length, to);
        break;
    }                
}            

static const char *info_frame_type_names[16] = {
    "FIS-B APDU",
    "Reserved for Developmental Use",
    "Reserved for Future Use (2)",
    "Reserved for Future Use (3)",
    "Reserved for Future Use (4)",
    "Reserved for Future Use (5)",
    "Reserved for Future Use (6)",
    "Reserved for Future Use (7)",
    "Reserved for Future Use (8)",
    "Reserved for Future Use (9)",
    "Reserved for Future Use (10)",
    "Reserved for Future Use (11)",
    "Reserved for Future Use (12)",
    "Reserved for Future Use (13)",
    "Reserved for Future Use (14)",
    "TIS-B/ADS-R Service Status"
};

static void uat_display_uplink_info_frame(const struct uat_uplink_info_frame *frame, FILE *to)
{
    fprintf(to,
            "INFORMATION FRAME:\n"
            " Length:            %u bytes\n"
            " Type:              %u (%s)\n",
            frame->length,
            frame->type,
            info_frame_type_names[frame->type]);

    if (frame->length > 0) {
        if (frame->is_fisb)
            uat_display_fisb_frame(&frame->fisb, to);
        else {
            display_generic_data(frame->data, frame->length, to);
        }
    }
}

void uat_display_uplink_mdb(const struct uat_uplink_mdb *mdb, FILE *to)
{
    fprintf(to, 
            "UPLINK:\n");

    fprintf(to,
            " Site Latitude:     %+.4f%s\n"
            " Site Longitude:    %+.4f%s\n",
            mdb->lat, mdb->position_valid ? "" : " (possibly invalid)",
            mdb->lon, mdb->position_valid ? "" : " (possibly invalid)");
            
    fprintf(to,
            " UTC coupled:       %s\n"
            " Slot ID:           %u\n"
            " TIS-B Site ID:     %u\n",
            mdb->utc_coupled ? "yes" : "no",
            mdb->slot_id,
            mdb->tisb_site_id);
    
    if (mdb->app_data_valid) {
        unsigned i;
        for (i = 0; i < mdb->num_info_frames; ++i)
            uat_display_uplink_info_frame(&mdb->info_frames[i], to);
    }
}
