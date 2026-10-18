// receiver load measured with dect2cli's own meter (% of real time = CPU cores busy), 0.2.4 and 0.2.7 built from their sources, the same recording, two runs each (footage/speed_027.sh):
// ISDB-T 140, 146 -> 76, 80 %; DVB-T 100, 109 -> 91, 98 %; ATSC and DAB unchanged (245, 248 -> 239, 247; 8 -> 8 to 9)
window.SPEED7=[{mode:'ISDB-T',from:{v:'0.2.4',cores:1.43},to:{v:'0.2.7',cores:.78}},{mode:'DVB-T',from:{v:'0.2.4',cores:1.05},to:{v:'0.2.7',cores:.95}}];
