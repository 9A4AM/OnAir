# SDDC_FX3.img (RX888 MkII USB firmware)

OnAir copies this file into its `codecs` folder in official packages. The RX888 MkII Cypress FX3 has no
persistent program: the host loads this image into **RAM** when the radio enumerates as `04b4:00f3`. Nothing is
written to the radio's flash; unplugging clears it (same as HDSDR / SDDC tools).

Source: [SDDC_Driver](https://github.com/renardspark/SDDC_Driver) release **v2.0.0** (`SDDC_FX3.img`).
The driver project is MIT-licensed; the image contains Cypress FX3 SDK material under Cypress/Infineon's terms
(see the SDDC_Driver repository).

Do not replace this file with a random download unless it is the official SDDC_FX3.img from that project.
