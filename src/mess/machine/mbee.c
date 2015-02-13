/***************************************************************************

    microbee.c

    machine driver
    Juergen Buchmueller <pullmoll@t-online.de>, Jan 2000


****************************************************************************/

#include "includes/mbee.h"
#include "machine/z80bin.h"


void mbee_state::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	switch (id)
	{
	case TIMER_MBEE256_KBD:
		mbee256_kbd(ptr, param);
		break;
	case TIMER_MBEE_RTC_IRQ:
		mbee_rtc_irq(ptr, param);
		break;
	case TIMER_MBEE_RESET:
		mbee_reset(ptr, param);
		break;
	default:
		assert_always(FALSE, "Unknown id in mbee_state::device_timer");
	}
}


/***********************************************************

    PIO

************************************************************/

WRITE_LINE_MEMBER( mbee_state::pio_ardy )
{
	m_centronics->write_strobe((state) ? 0 : 1);
}

WRITE8_MEMBER( mbee_state::pio_port_a_w )
{
	/* hardware strobe driven by PIO ARDY, bit 7..0 = data */
	m_pio->strobe_a(1); /* needed - otherwise nothing prints */
	m_cent_data_out->write(space, 0, data);
};

WRITE8_MEMBER( mbee_state::pio_port_b_w )
{
/*  PIO port B - d5..d2 not emulated
    d7 network interrupt (microbee network for classrooms)
    d6 speaker
    d5 rs232 output (1=mark)
    d4 rs232 input (0=mark)
    d3 rs232 CTS (0=clear to send)
    d2 rs232 clock or DTR
    d1 cass out and (on 256tc) keyboard irq
    d0 cass in */

	m_cassette->output((data & 0x02) ? -1.0 : +1.0);

	m_speaker->level_w(BIT(data, 6));
};

READ8_MEMBER( mbee_state::pio_port_b_r )
{
	UINT8 data = 0;

	if (m_cassette->input() > 0.03) data |= 1;

	switch (m_io_config->read() & 0xc0)
	{
		case 0x00:
			data |= (UINT8)m_b7_vs << 7;
			break;
		case 0x40:
			data |= (UINT8)m_b7_rtc << 7;
			break;
		case 0x80:
			data |= 0x80;
			break;
		case 0xc0:
			data |= 0x80; // centronics busy line - FIXME
			break;
	}

	data |= (UINT8)m_b2 << 1; // key pressed on new keyboard
	data |= 8; // CTS held high via resistor. If low, the disk-based models think a mouse is plugged in.

	return data;
};

/*************************************************************************************

    Floppy Disk

    The callback is quite simple, no interrupts are used.
    If either IRQ or DRQ activate, they set bit 7 of inport 0x48.

*************************************************************************************/

WRITE_LINE_MEMBER( mbee_state::fdc_intrq_w )
{
	m_fdc_rq = (m_fdc_rq & 2) | state;
}

WRITE_LINE_MEMBER( mbee_state::fdc_drq_w )
{
	m_fdc_rq = (m_fdc_rq & 1) | (state << 1);
}

READ8_MEMBER( mbee_state::mbee_fdc_status_r )
{
/*  d7 indicate if IRQ or DRQ is occurring (1=happening)
    d6..d0 not used */

	return m_fdc_rq ? 0xff : 0x7f;
}

WRITE8_MEMBER( mbee_state::mbee_fdc_motor_w )
{
/*  d7..d4 not used
    d3 density (1=MFM)
    d2 side (1=side 1)
    d1..d0 drive select (0 to 3) */

	floppy_image_device *floppy = NULL;
	if ((data&3)==0)
		floppy = m_floppy0->get_device();
	else
	if ((data&3)==1)
		floppy = m_floppy1->get_device();

	m_fdc->set_floppy(floppy);
	m_fdc->dden_w(!BIT(data, 3)); // /Q output of ic29

	if (floppy)
	{
		floppy->mon_w(0); // motor on
		floppy->ss_w(BIT(data, 2)); // inverted on the board
	}
}

/***********************************************************

    256TC Keyboard

************************************************************/


TIMER_CALLBACK_MEMBER(mbee_state::mbee256_kbd)
{
	/* Keyboard scanner is a Mostek M3870 chip. Its speed of operation is determined by a 15k resistor on
	pin 2 (XTL2) and is therefore 2MHz. If a key change is detected (up or down), the /strobe
	line activates, sending a high to bit 1 of port 2 (one of the pio input lines). The next read of
	port 18 will clear this line, and read the key scancode. It will also signal the 3870 that the key
	data has been read, on pin 38 (/extint). The 3870 can cache up to 9 keys. With no rom dump
	available, the following is a guess.

	The 3870 (MK3870) 8-bit microcontroller is a single chip implementation of Fairchild F8 (Mostek 3850).
	It includes up to 4 KB of mask-programmable ROM, 64 bytes of scratchpad RAM and up to 64 bytes
	of executable RAM. The MCU also integrates 32-bit I/O and a programmable timer. */

	UINT8 i, j, pressed;

	// find what has changed
	for (i = 0; i < 15; i++)
	{
		pressed = m_io_newkb[i]->read();
		if (pressed != m_mbee256_was_pressed[i])
		{
			// get scankey value
			for (j = 0; j < 8; j++)
			{
				if (BIT(pressed^m_mbee256_was_pressed[i], j))
				{
					// put it in the queue
					m_mbee256_q[m_mbee256_q_pos] = (i << 3) | j | (BIT(pressed, j) ? 0x80 : 0);
					if (m_mbee256_q_pos < 19) m_mbee256_q_pos++;
				}
			}
			m_mbee256_was_pressed[i] = pressed;
		}
	}

	// if anything queued, cause an interrupt
	if (m_mbee256_q_pos)
	{
		m_b2 = 1; // set irq
		//breaks keyboard m_pio->port_b_write(pio_port_b_r(generic_space(),0,0xff));
	}

	timer_set(attotime::from_hz(25), TIMER_MBEE256_KBD);
}

READ8_MEMBER( mbee_state::mbee256_18_r )
{
	UINT8 i, data = m_mbee256_q[0]; // get oldest key

	if (m_mbee256_q_pos)
	{
		m_mbee256_q_pos--;
		for (i = 0; i < m_mbee256_q_pos; i++) m_mbee256_q[i] = m_mbee256_q[i+1]; // ripple queue
	}

	m_b2 = 0; // clear irq
	return data;
}


/***********************************************************

    256TC Change CPU speed

************************************************************/

READ8_MEMBER( mbee_state::mbee256_speed_low_r )
{
	m_maincpu->set_unscaled_clock(3375000);
	return 0xff;
}

READ8_MEMBER( mbee_state::mbee256_speed_high_r )
{
	m_maincpu->set_unscaled_clock(6750000);
	return 0xff;
}



/***********************************************************

    256TC Real Time Clock

************************************************************/

WRITE8_MEMBER( mbee_state::mbee_04_w )  // address
{
	m_rtc->write(space, 0, data);
}

WRITE8_MEMBER( mbee_state::mbee_06_w )  // write
{
	m_rtc->write(space, 1, data);
}

READ8_MEMBER( mbee_state::mbee_07_r )   // read
{
	return m_rtc->read(space, 1);
}

// This doesn't seem to do anything; the time works without it.
TIMER_CALLBACK_MEMBER( mbee_state::mbee_rtc_irq )
{
	if (!m_rtc)
		return;

	UINT8 data = m_rtc->read(m_maincpu->space(AS_IO), 12);
	m_b7_rtc = (data) ? 1 : 0;

	if ((m_io_config->read() & 0xc0) == 0x40) // RTC selected in config menu
		m_pio->port_b_write(pio_port_b_r(generic_space(),0,0xff));

	timer_set(attotime::from_hz(10), TIMER_MBEE_RTC_IRQ);
}


/***********************************************************

    256TC Memory Banking

    Selection of ROM, RAM, and video access by the CPU is controlled by U39,
    a PAL14L8. When read as an ordinary rom it is 16k in size. The dumper has
    arranged the pins as (bit0,1,..) (input = 1,23,2,3,4,5,6,7,8,9,10,11,14,13)
    and (output = 22,21,20,19,18,17,16,15). The prom is also used to control
    the refresh required by the dynamic rams, however we ignore this function.

    b_mask = total dynamic ram (1=64k; 3=128k; 7=256k)

    Certain software (such as the PJB system) constantly switch banks around,
    causing slowness. Therefore this function only changes the banks that need
    changing, leaving the others as is.

************************************************************/

void mbee_state::setup_banks(UINT8 data, bool first_time, UINT8 b_mask)
{
	data &= 0x3f; // (bits 0-5 are referred to as S0-S5)
	address_space &mem = m_maincpu->space(AS_PROGRAM);
	UINT8 *prom = memregion("pals")->base();
	UINT8 b_data = BITSWAP8(data, 7,5,3,2,4,6,1,0) & 0x3b; // arrange data bits to S0,S1,-,S4,S2,S3
	UINT8 b_bank, b_byte, b_byte_t, b_addr, p_bank = 1;
	UINT16 b_vid;
	char banktag[10];

	if (first_time || (b_data != m_bank_array[0])) // if same data as last time, leave now
	{
		m_bank_array[0] = b_data;

		for (b_bank = 0; b_bank < 16; b_bank++)
		{
			b_vid = b_bank << 12;
			b_addr = BITSWAP8(b_bank, 7,4,5,3,1,2,6,0) & 0x1f; // arrange address bits to A12,-,A14,A13,A15

			// Calculate read-bank
			b_byte_t = prom[b_addr | (b_data << 8) | 0x82]; // read-bank (RDS and MREQ are low, RFSH is high)
			b_byte = BITSWAP8(b_byte_t, 7,5,0,3,6,2,1,4); // rearrange so that bits 0-2 are rambank, bit 3 = rom select, bit 4 = video select, others not used

			if (first_time || (b_byte != m_bank_array[p_bank]))
			{
				m_bank_array[p_bank] = b_byte;

				if (!BIT(data, 5))
					b_byte &= 0xfb;  // U42/1 - S17 only valid if S5 is on

				mem.unmap_read (b_vid, b_vid + 0xfff);

				if (!BIT(b_byte, 4))
				{
					// select video
					if (m_is_premium)
					{
						mem.install_read_handler (b_vid, b_vid + 0x7ff, read8_delegate(FUNC(mbee_state::mbeeppc_low_r), this));
						mem.install_read_handler (b_vid + 0x800, b_vid + 0xfff, read8_delegate(FUNC(mbee_state::mbeeppc_high_r), this));
					}
					else
					{
						mem.install_read_handler (b_vid, b_vid + 0x7ff, read8_delegate(FUNC(mbee_state::mbee_low_r), this));
						mem.install_read_handler (b_vid + 0x800, b_vid + 0xfff, read8_delegate(FUNC(mbee_state::mbeeic_high_r), this));
					}
				}
				else
				{
					sprintf(banktag, "bankr%d", b_bank);
					mem.install_read_bank( b_vid, b_vid+0xfff, banktag );

					if (!BIT(b_byte, 3))
						membank(banktag)->set_entry(64 + (b_bank & 3)); // read from rom
					else
						membank(banktag)->set_entry((b_bank & 7) | ((b_byte & b_mask) << 3)); // ram
				}
			}
			p_bank++;

			// Calculate write-bank
			b_byte_t = prom[b_addr | (b_data << 8) | 0xc0]; // write-bank (XWR and MREQ are low, RFSH is high)
			b_byte = BITSWAP8(b_byte_t, 7,5,0,3,6,2,1,4); // rearrange so that bits 0-2 are rambank, bit 3 = rom select, bit 4 = video select, others not used

			if (first_time || (b_byte != m_bank_array[p_bank]))
			{
				m_bank_array[p_bank] = b_byte;

				if (!BIT(data, 5))
					b_byte &= 0xfb;  // U42/1 - S17 only valid if S5 is on

				mem.unmap_write (b_vid, b_vid + 0xfff);

				if (!BIT(b_byte, 4))
				{
					// select video
					if (m_is_premium)
					{
						mem.install_write_handler (b_vid, b_vid + 0x7ff, write8_delegate(FUNC(mbee_state::mbeeppc_low_w), this));
						mem.install_write_handler (b_vid + 0x800, b_vid + 0xfff, write8_delegate(FUNC(mbee_state::mbeeppc_high_w), this));
					}
					else
					{
						mem.install_write_handler (b_vid, b_vid + 0x7ff, write8_delegate(FUNC(mbee_state::mbee_low_w), this));
						mem.install_write_handler (b_vid + 0x800, b_vid + 0xfff, write8_delegate(FUNC(mbee_state::mbeeic_high_w), this));
					}
				}
				else
				{
					sprintf(banktag, "bankw%d", b_bank);
					mem.install_write_bank( b_vid, b_vid+0xfff, banktag );

					if (!BIT(b_byte, 3))
						membank(banktag)->set_entry(64); // write to rom dummy area
					else
						membank(banktag)->set_entry((b_bank & 7) | ((b_byte & b_mask) << 3)); // ram
				}
			}
			p_bank++;
		}
	}
}

WRITE8_MEMBER( mbee_state::mbee256_50_w )
{
	setup_banks(data, 0, 7);
}

/***********************************************************

    128k Memory Banking

    The only difference to the 256TC is that bit 5 switches
    between rom2 and rom3. Since neither of these is dumped,
    this bit is not emulated. If it was, this scheme is used:

    Low - rom2 occupies C000-FFFF
    High - ram = C000-DFFF, rom3 = E000-FFFF.

************************************************************/

WRITE8_MEMBER( mbee_state::mbee128_50_w )
{
	setup_banks(data, 0, 3);
}

/***********************************************************

    ROM Banking on older models

    Set A to 0 or 1 then read the port to switch between the
    halves of the Telcom ROM.

    Output the PAK number to choose an optional PAK ROM.

    The bios will support 256 PAKs, although normally only
    8 are available in hardware. Each PAK is normally a 4K
    ROM. If 8K ROMs are used, the 2nd half becomes PAK+8,
    thus 16 PAKs in total. This is used in the PC85 models.

************************************************************/

READ8_MEMBER( mbee_state::mbeeic_0a_r )
{
	return m_0a;
}

WRITE8_MEMBER( mbee_state::mbeeic_0a_w )
{
	m_0a = data;
	m_pak->set_entry(data & 15);
}

READ8_MEMBER( mbee_state::mbeepc_telcom_low_r )
{
/* Read of port 0A - set Telcom rom to first half */
	m_telcom->set_entry(0);
	return m_0a;
}

READ8_MEMBER( mbee_state::mbeepc_telcom_high_r )
{
/* Read of port 10A - set Telcom rom to 2nd half */
	m_telcom->set_entry(1);
	return m_0a;
}


/***********************************************************

    Machine

************************************************************/

/*
  On reset or power on, a circuit forces rom 8000-8FFF to appear at 0000-0FFF, while ram is disabled.
  It gets set back to normal on the first attempt to write to memory. (/WR line goes active).
*/


/* after the first 4 bytes have been read from ROM, switch the ram back in */
TIMER_CALLBACK_MEMBER( mbee_state::mbee_reset )
{
	m_boot->set_entry(0);
}

void mbee_state::machine_reset_common_disk()
{
	m_fdc_rq = 0;
}

MACHINE_RESET_MEMBER( mbee_state, mbee )
{
	m_boot->set_entry(1);
	timer_set(attotime::from_usec(4), TIMER_MBEE_RESET);
}

MACHINE_RESET_MEMBER( mbee_state, mbee56 )
{
	machine_reset_common_disk();
	m_boot->set_entry(1);
	timer_set(attotime::from_usec(4), TIMER_MBEE_RESET);
}

MACHINE_RESET_MEMBER( mbee_state, mbee128 )
{
	machine_reset_common_disk();
	setup_banks(0, 1, 3); // set banks to default
	m_maincpu->set_pc(0x8000);
}

MACHINE_RESET_MEMBER( mbee_state, mbee256 )
{
	UINT8 i;
	for (i = 0; i < 15; i++) m_mbee256_was_pressed[i] = 0;
	m_mbee256_q_pos = 0;
	machine_reset_common_disk();
	setup_banks(0, 1, 7); // set banks to default
	m_maincpu->set_pc(0x8000);
}

MACHINE_RESET_MEMBER( mbee_state, mbeett )
{
	UINT8 i;
	for (i = 0; i < 15; i++) m_mbee256_was_pressed[i] = 0;
	m_mbee256_q_pos = 0;
	m_boot->set_entry(1);
	timer_set(attotime::from_usec(4), TIMER_MBEE_RESET);
}

INTERRUPT_GEN_MEMBER( mbee_state::mbee_interrupt )
{
// Due to the uncertainly and hackage here, this is commented out for now - Robbbert - 05-Oct-2010
#if 0

	//address_space &space = m_maincpu->space(AS_PROGRAM);
	/* The printer status connects to the pio ASTB pin, and the printer changing to not
	    busy should signal an interrupt routine at B61C, (next line) but this doesn't work.
	    The line below does what the interrupt should be doing. */
	/* But it would break any program loaded to that area of memory, such as CP/M programs */

	//m_z80pio->strobe_a(centronics_busy_r(m_centronics)); /* signal int when not busy (L->H) */
	//space.write_byte(0x109, centronics_busy_r(m_centronics));


	/* once per frame, pulse the PIO B bit 7 - it is in the schematic as an option,
	but need to find out what it does */
	m_b7_busy = 0x80;
	irq0_line_hold(device);

#endif
}

DRIVER_INIT_MEMBER( mbee_state, mbee )
{
	UINT8 *RAM = memregion("maincpu")->base();
	m_boot->configure_entries(0, 2, &RAM[0x0000], 0x8000);
	m_size = 0x4000;
	m_has_oldkb = 1;
}

DRIVER_INIT_MEMBER( mbee_state, mbeeic )
{
	UINT8 *RAM = memregion("maincpu")->base();
	m_boot->configure_entries(0, 2, &RAM[0x0000], 0x8000);

	RAM = memregion("pakrom")->base();
	m_pak->configure_entries(0, 16, &RAM[0x0000], 0x2000);

	m_pak->set_entry(0);
	m_size = 0x8000;
	m_has_oldkb = 1;
}

DRIVER_INIT_MEMBER( mbee_state, mbeepc )
{
	UINT8 *RAM = memregion("maincpu")->base();
	m_boot->configure_entries(0, 2, &RAM[0x0000], 0x8000);

	RAM = memregion("telcomrom")->base();
	m_telcom->configure_entries(0, 2, &RAM[0x0000], 0x1000);

	RAM = memregion("pakrom")->base();
	m_pak->configure_entries(0, 16, &RAM[0x0000], 0x2000);

	m_pak->set_entry(0);
	m_telcom->set_entry(0);
	m_size = 0x8000;
	m_has_oldkb = 1;
}

DRIVER_INIT_MEMBER( mbee_state, mbeepc85 )
{
	UINT8 *RAM = memregion("maincpu")->base();
	m_boot->configure_entries(0, 2, &RAM[0x0000], 0x8000);

	RAM = memregion("telcomrom")->base();
	m_telcom->configure_entries(0, 2, &RAM[0x0000], 0x1000);

	RAM = memregion("pakrom")->base();
	m_pak->configure_entries(0, 16, &RAM[0x0000], 0x2000);

	m_pak->set_entry(5);
	m_telcom->set_entry(0);
	m_size = 0x8000;
	m_has_oldkb = 1;
}

DRIVER_INIT_MEMBER( mbee_state, mbeeppc )
{
	UINT8 *RAM = memregion("maincpu")->base();
	m_boot->configure_entry(0, &RAM[0x0000]);

	RAM = memregion("basicrom")->base();
	m_basic->configure_entries(0, 2, &RAM[0x0000], 0x2000);
	m_boot->configure_entry(1, &RAM[0x0000]);

	RAM = memregion("telcomrom")->base();
	m_telcom->configure_entries(0, 2, &RAM[0x0000], 0x1000);

	RAM = memregion("pakrom")->base();
	m_pak->configure_entries(0, 16, &RAM[0x0000], 0x2000);

	m_pak->set_entry(5);
	m_telcom->set_entry(0);
	m_basic->set_entry(0);
	m_size = 0x8000;
	m_has_oldkb = 1;
}

DRIVER_INIT_MEMBER( mbee_state, mbee56 )
{
	UINT8 *RAM = memregion("maincpu")->base();
	m_boot->configure_entries(0, 2, &RAM[0x0000], 0xe000);
	m_size = 0xe000;
	m_has_oldkb = 1;
}

DRIVER_INIT_MEMBER( mbee_state, mbee128 )
{
	UINT8 *RAM = memregion("rams")->base();
	UINT8 *ROM = memregion("roms")->base();
	char banktag[10];

	for (UINT8 b_bank = 0; b_bank < 16; b_bank++)
	{
		sprintf(banktag, "bankr%d", b_bank);
		membank(banktag)->configure_entries(0, 32, &RAM[0x0000], 0x1000); // RAM banks
		membank(banktag)->configure_entries(64, 4, &ROM[0x0000], 0x1000); // rom

		sprintf(banktag, "bankw%d", b_bank);
		membank(banktag)->configure_entries(0, 32, &RAM[0x0000], 0x1000); // RAM banks
		membank(banktag)->configure_entries(64, 1, &ROM[0x4000], 0x1000); // dummy rom
	}

	timer_set(attotime::from_hz(1), TIMER_MBEE_RTC_IRQ);   /* timer for rtc */

	m_size = 0x8000;
	m_has_oldkb = 1;
}

DRIVER_INIT_MEMBER( mbee_state, mbee256 )
{
	UINT8 *RAM = memregion("rams")->base();
	UINT8 *ROM = memregion("roms")->base();
	char banktag[10];

	for (UINT8 b_bank = 0; b_bank < 16; b_bank++)
	{
		sprintf(banktag, "bankr%d", b_bank);
		membank(banktag)->configure_entries(0, 64, &RAM[0x0000], 0x1000); // RAM banks
		membank(banktag)->configure_entries(64, 4, &ROM[0x0000], 0x1000); // rom

		sprintf(banktag, "bankw%d", b_bank);
		membank(banktag)->configure_entries(0, 64, &RAM[0x0000], 0x1000); // RAM banks
		membank(banktag)->configure_entries(64, 1, &ROM[0x4000], 0x1000); // dummy rom
	}

	timer_set(attotime::from_hz(1), TIMER_MBEE_RTC_IRQ);   /* timer for rtc */
	timer_set(attotime::from_hz(50), TIMER_MBEE256_KBD);   /* timer for kbd */

	m_size = 0x8000;
	m_has_oldkb = 0;
}

DRIVER_INIT_MEMBER( mbee_state, mbeett )
{
	UINT8 *RAM = memregion("maincpu")->base();
	m_boot->configure_entries(0, 2, &RAM[0x0000], 0x8000);

	RAM = memregion("telcomrom")->base();
	m_telcom->configure_entries(0, 2, &RAM[0x0000], 0x1000);

	RAM = memregion("pakrom")->base();
	m_pak->configure_entries(0, 16, &RAM[0x0000], 0x2000);

	m_pak->set_entry(5);
	m_telcom->set_entry(0);

	timer_set(attotime::from_hz(1), TIMER_MBEE_RTC_IRQ);   /* timer for rtc */
	timer_set(attotime::from_hz(25), TIMER_MBEE256_KBD);   /* timer for kbd */

	m_size = 0x8000;
	m_has_oldkb = 0;
}


/***********************************************************

    Quickload

    These load the standard BIN format, as well
    as BEE, COM and MWB files.

************************************************************/

QUICKLOAD_LOAD_MEMBER( mbee_state, mbee )
{
	address_space &space = m_maincpu->space(AS_PROGRAM);
	UINT16 i, j;
	UINT8 data, sw = m_io_config->read() & 1;   /* reading the config switch: 1 = autorun */

	if (!core_stricmp(image.filetype(), "mwb"))
	{
		/* mwb files - standard basic files */
		for (i = 0; i < quickload_size; i++)
		{
			j = 0x8c0 + i;

			if (image.fread(&data, 1) != 1)
			{
				image.message("Unexpected EOF");
				return IMAGE_INIT_FAIL;
			}

			if ((j < m_size) || (j > 0xefff))
				space.write_byte(j, data);
			else
			{
				image.message("Not enough memory in this microbee");
				return IMAGE_INIT_FAIL;
			}
		}

		if (sw)
		{
			space.write_word(0xa2,0x801e);  /* fix warm-start vector to get around some copy-protections */
			m_maincpu->set_pc(0x801e);
		}
		else
			space.write_word(0xa2,0x8517);
	}
	else if (!core_stricmp(image.filetype(), "com"))
	{
		/* com files - most com files are just machine-language games with a wrapper and don't need cp/m to be present */
		for (i = 0; i < quickload_size; i++)
		{
			j = 0x100 + i;

			if (image.fread(&data, 1) != 1)
			{
				image.message("Unexpected EOF");
				return IMAGE_INIT_FAIL;
			}

			if ((j < m_size) || (j > 0xefff))
				space.write_byte(j, data);
			else
			{
				image.message("Not enough memory in this microbee");
				return IMAGE_INIT_FAIL;
			}
		}

		if (sw) m_maincpu->set_pc(0x100);
	}
	else if (!core_stricmp(image.filetype(), "bee"))
	{
		/* bee files - machine-language games that start at 0900 */
		for (i = 0; i < quickload_size; i++)
		{
			j = 0x900 + i;

			if (image.fread(&data, 1) != 1)
			{
				image.message("Unexpected EOF");
				return IMAGE_INIT_FAIL;
			}

			if ((j < m_size) || (j > 0xefff))
				space.write_byte(j, data);
			else
			{
				image.message("Not enough memory in this microbee");
				return IMAGE_INIT_FAIL;
			}
		}

		if (sw) m_maincpu->set_pc(0x900);
	}

	return IMAGE_INIT_PASS;
}


/*-------------------------------------------------
    QUICKLOAD_LOAD_MEMBER( mbee_state, mbee_z80bin )
-------------------------------------------------*/

QUICKLOAD_LOAD_MEMBER( mbee_state, mbee_z80bin )
{
	UINT16 execute_address, start_addr, end_addr;
	int autorun;

	/* load the binary into memory */
	if (z80bin_load_file(&image, file_type, &execute_address, &start_addr, &end_addr) == IMAGE_INIT_FAIL)
		return IMAGE_INIT_FAIL;

	/* is this file executable? */
	if (execute_address != 0xffff)
	{
		/* check to see if autorun is on */
		autorun = m_io_config->read_safe(0xFF) & 1;

		address_space &space = m_maincpu->space(AS_PROGRAM);

		space.write_word(0xa6, execute_address);            /* fix the EXEC command */

		if (autorun)
		{
			space.write_word(0xa2, execute_address);        /* fix warm-start vector to get around some copy-protections */
			m_maincpu->set_pc(execute_address);
		}
		else
		{
			space.write_word(0xa2, 0x8517);
		}
	}

	return IMAGE_INIT_PASS;
}
