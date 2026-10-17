// Label > category table for acars_log.cpp. Labels are the two-character ARINC 618/620 label (facts from the standards as the decoders
// list them; "_d" is the DEL form). cat: I ignore (never shown unless "Show all"), F free text, O operations, C company, M maintenance.
// A label that is not here is judged by its text alone.
//   label  cat
{"_d", 'I'},   // command / response (technical acknowledgement)
{"Q0", 'I'},   // link test
{"SQ", 'I'},   // ground station squitter
{"SA", 'I'},   // media advisory
{"Q1", 'O'}, {"Q2", 'O'}, {"QA", 'O'}, {"QB", 'O'}, {"QC", 'O'}, {"QD", 'O'}, {"QE", 'O'}, {"QF", 'O'}, {"QG", 'O'}, {"QH", 'O'},
{"QK", 'O'}, {"QL", 'O'}, {"QM", 'O'}, {"QN", 'O'}, {"QP", 'O'}, {"QQ", 'O'}, {"QR", 'O'}, {"QS", 'O'}, {"QT", 'O'},   // OOOI and ETA reports
{"10", 'O'}, {"15", 'O'}, {"17", 'O'}, {"2N", 'O'}, {"44", 'O'},    // arrival, flight status, ETA, takeoff, position or ETA reports
{"H1", 'O'},   // terminal message: flight plans, weather, loadsheets, company text
{"5Z", 'C'},   // airline defined downlink
{"5Y", 'C'}, {"5U", 'C'},   // airline defined downlinks
{"80", 'F'}, {"81", 'F'}, {"82", 'F'}, {"83", 'F'},   // free text
// Maintenance and fault messages are found by their text (keywords in acars_log.cpp), not by label.
