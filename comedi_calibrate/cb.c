/***************************************************************************
	cb.c  -  calibration support for some Measurement computing boards.
	Based on ni.c by David Schleef.
                             -------------------

    begin                : Sat Apr 27 2002
    copyright            : (C) 2002,2003 by Frank Mori Hess
    email                : fmhess@users.sourceforge.net

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by                                                          *
 *   the Free Software Foundation; either version 2.1 of the License, or   *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#define _GNU_SOURCE

#include <stdio.h>
#include <comedilib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "calib.h"


char cb_id[] = "$Id$";

struct board_struct{
	char *name;
	int status;
	int (*setup)( calibration_setup_t *setup );
};

static int setup_cb_pci_1xxx( calibration_setup_t *setup );
static int setup_cb_pci_1001( calibration_setup_t *setup );
static int setup_cb_pci_1602_16( calibration_setup_t *setup );

static int cal_cb_pci_1xxx( calibration_setup_t *setup );
static int cal_cb_pci_1001( calibration_setup_t *setup );
static int cal_cb_pci_1602_16( calibration_setup_t *setup );

static int init_observables_1xxx( calibration_setup_t *setup );
static int init_observables_1001( calibration_setup_t *setup );
static int init_observables_1602_16( calibration_setup_t *setup );

static struct board_struct boards[]={
	{ "pci-das1000",	STATUS_GUESS,	setup_cb_pci_1xxx },
	{ "pci-das1001",	STATUS_GUESS,	setup_cb_pci_1001 },
	{ "pci-das1002",	STATUS_GUESS,	setup_cb_pci_1xxx },
	{ "pci-das1200",	STATUS_DONE,	setup_cb_pci_1xxx },
	{ "pci-das1200/jr",	STATUS_GUESS,	setup_cb_pci_1xxx },
	{ "pci-das1602/12",	STATUS_GUESS,	setup_cb_pci_1xxx },
	{ "pci-das1602/16",	STATUS_GUESS,	setup_cb_pci_1602_16 },
	{ "pci-das1602/16/jr",	STATUS_GUESS,	setup_cb_pci_1602_16 },
};

static const int num_boards = ( sizeof(boards) / sizeof(boards[0]) );

enum observables_1xxx {
	OBS_0V_RANGE_10V_BIP_1XXX = 0,
	OBS_7V_RANGE_10V_BIP_1XXX,
	OBS_DAC0_GROUND_1XXX,
	OBS_DAC0_HIGH_1XXX,
	OBS_DAC1_GROUND_1XXX,
	OBS_DAC1_HIGH_1XXX,
};

enum observables_1602_16 {
	OBS_0V_RANGE_10V_BIP_1602_16 = 0,
	OBS_7V_RANGE_10V_BIP_1602_16,
};

int cb_setup( calibration_setup_t *setup, const char *device_name )
{
	unsigned int i;

	for( i = 0; i < num_boards; i++ )
	{
		if( !strcmp( devicename, boards[i].name ) )
		{
			setup->status = boards[i].status;
			return boards[i].setup( setup );
			break;
		}
	}
	if( i == num_boards ) return -1;

	return 0;
}

static int setup_cb_pci_1xxx( calibration_setup_t *setup )
{
	static const int caldac_subdev = 4;
	static const int calpot_subdev = 5;

	init_observables_1xxx( setup );
	setup_caldacs( setup, caldac_subdev );
	setup_caldacs( setup, calpot_subdev );
	setup->do_cal = cal_cb_pci_1xxx;
	return 0;
}

static int setup_cb_pci_1001( calibration_setup_t *setup )
{
	static const int caldac_subdev = 4;
	static const int calpot_subdev = 5;

	init_observables_1001( setup );
	setup_caldacs( setup, caldac_subdev );
	setup_caldacs( setup, calpot_subdev );
	setup->do_cal = cal_cb_pci_1001;
	return 0;
}

static int setup_cb_pci_1602_16( calibration_setup_t *setup )
{
	static const int caldac_subdev = 4;
	static const int calpot_subdev = 5;
	static const int dac08_subdev = 6;

	init_observables_1602_16( setup );
	setup_caldacs( setup, caldac_subdev );
	setup_caldacs( setup, calpot_subdev );
	setup_caldacs( setup, dac08_subdev );
	setup->do_cal = cal_cb_pci_1602_16;
	return 0;
}

static int init_observables_1xxx( calibration_setup_t *setup )
{
	comedi_insn tmpl, po_tmpl;
	observable *o;
	int retval;
	float target;
	enum source_eeprom_addr
	{
		EEPROM_7V_CHAN = 0x80,
		EEPROM_3500mV_CHAN = 0x84,
		EEPROM_1750mV_CHAN = 0x88,
		EEPROM_875mV_CHAN = 0x8c,
		EEPROM_8600uV_CHAN = 0x90,
	};
	enum calibration_source
	{
		CAL_SRC_GROUND = 0,
		CAL_SRC_7V = 1,
		CAL_SRC_3500mV = 2,
		CAL_SRC_1750mV = 3,
		CAL_SRC_875mV = 4,
		CAL_SRC_8600uV = 5,
		CAL_SRC_DAC0 = 6,
		CAL_SRC_DAC1 = 7,
	};
	enum ai_ranges
	{
		AI_RNG_BIP_10V = 0
	};
	enum ao_ranges
	{
		AO_RNG_BIP_10V = 1,
	};

	setup->n_observables = 0;

	memset( &tmpl, 0, sizeof(tmpl) );
	tmpl.insn = INSN_READ;
	tmpl.n = 1;
	tmpl.subdev = setup->ad_subdev;

	o = setup->observables + OBS_0V_RANGE_10V_BIP_1XXX;
	o->name = "ground calibration source, 10V bipolar range, ground referenced";
	o->reference_source = CAL_SRC_GROUND;
	o->observe_insn = tmpl;
	o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
		CR_ALT_SOURCE | CR_ALT_FILTER;
	o->target = 0.0;
	setup->n_observables++;

	o = setup->observables + OBS_7V_RANGE_10V_BIP_1XXX;
	o->name = "7V calibration source, 10V bipolar range, ground referenced";
	o->reference_source = CAL_SRC_7V;
	o->observe_insn = tmpl;
	o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
		CR_ALT_SOURCE | CR_ALT_FILTER;
	o->target = 7.0;
	retval = cb_actual_source_voltage( setup->dev, setup->eeprom_subdev, EEPROM_7V_CHAN, &target );
	if( retval == 0 )
		o->target = target;
	setup->n_observables++;

	if( setup->da_subdev >= 0 )
	{
		memset( &po_tmpl, 0, sizeof(po_tmpl) );
		po_tmpl.insn = INSN_WRITE;
		po_tmpl.n = 1;
		po_tmpl.subdev = setup->da_subdev;

		o = setup->observables + OBS_DAC0_GROUND_1XXX;
		o->name = "DAC0 ground calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC0;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 0, AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC0_GROUND_1XXX, 0.0 );
		setup->n_observables++;

		o = setup->observables + OBS_DAC0_HIGH_1XXX;
		o->name = "DAC0 high calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC0;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 0 , AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC0_HIGH_1XXX, 8.0 );
		setup->n_observables++;

		o = setup->observables + OBS_DAC1_GROUND_1XXX;
		o->name = "DAC1 ground calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC1;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 1, AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC1_GROUND_1XXX, 0.0 );
		setup->n_observables++;

		o = setup->observables + OBS_DAC1_HIGH_1XXX;
		o->name = "DAC1 high calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC1;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 1 , AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC1_HIGH_1XXX, 8.0 );
		setup->n_observables++;
	}

	return 0;
}

static int init_observables_1001( calibration_setup_t *setup )
{
	comedi_insn tmpl, po_tmpl;
	observable *o;
	int retval;
	float target;
	enum source_eeprom_addr
	{
		EEPROM_7V_CHAN = 0x80,
		EEPROM_88600uV_CHAN = 0x88,
		EEPROM_875mV_CHAN = 0x8c,
		EEPROM_8600uV_CHAN = 0x90,
	};
	enum calibration_source
	{
		CAL_SRC_GROUND = 0,
		CAL_SRC_7V = 1,
		CAL_SRC_88600uV = 3,
		CAL_SRC_875mV = 4,
		CAL_SRC_8600uV = 5,
		CAL_SRC_DAC0 = 6,
		CAL_SRC_DAC1 = 7,
	};
	enum ai_ranges
	{
		AI_RNG_BIP_10V = 0
	};
	enum ao_ranges
	{
		AO_RNG_BIP_10V = 1,
	};

	setup->n_observables = 0;

	memset( &tmpl, 0, sizeof(tmpl) );
	tmpl.insn = INSN_READ;
	tmpl.n = 1;
	tmpl.subdev = setup->ad_subdev;

	o = setup->observables + OBS_0V_RANGE_10V_BIP_1XXX;
	o->name = "ground calibration source, 10V bipolar range, ground referenced";
	o->reference_source = CAL_SRC_GROUND;
	o->observe_insn = tmpl;
	o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
		CR_ALT_SOURCE | CR_ALT_FILTER;
	o->target = 0.0;
	setup->n_observables++;

	o = setup->observables + OBS_7V_RANGE_10V_BIP_1XXX;
	o->name = "7V calibration source, 10V bipolar range, ground referenced";
	o->reference_source = CAL_SRC_7V;
	o->observe_insn = tmpl;
	o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
		CR_ALT_SOURCE | CR_ALT_FILTER;
	o->target = 7.0;
	retval = cb_actual_source_voltage( setup->dev, setup->eeprom_subdev, EEPROM_7V_CHAN, &target );
	if( retval == 0 )
		o->target = target;
	setup->n_observables++;

	if( setup->da_subdev >= 0 )
	{
		memset( &po_tmpl, 0, sizeof(po_tmpl) );
		po_tmpl.insn = INSN_WRITE;
		po_tmpl.n = 1;
		po_tmpl.subdev = setup->da_subdev;

		o = setup->observables + OBS_DAC0_GROUND_1XXX;
		o->name = "DAC0 ground calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC0;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 0, AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC0_GROUND_1XXX, 0.0 );
		setup->n_observables++;

		o = setup->observables + OBS_DAC0_HIGH_1XXX;
		o->name = "DAC0 high calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC0;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 0 , AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC0_HIGH_1XXX, 8.0 );
		setup->n_observables++;

		o = setup->observables + OBS_DAC1_GROUND_1XXX;
		o->name = "DAC1 ground calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC1;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 1, AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC1_GROUND_1XXX, 0.0 );
		setup->n_observables++;

		o = setup->observables + OBS_DAC1_HIGH_1XXX;
		o->name = "DAC1 high calibration source, 10V bipolar input range";
		o->reference_source = CAL_SRC_DAC1;
		o->preobserve_insn = po_tmpl;
		o->preobserve_insn.chanspec = CR_PACK( 1 , AO_RNG_BIP_10V, AREF_GROUND );
		o->preobserve_insn.data = o->preobserve_data;
		o->observe_insn = tmpl;
		o->observe_insn.chanspec = CR_PACK( 0, AI_RNG_BIP_10V, AREF_GROUND) |
			CR_ALT_SOURCE | CR_ALT_FILTER;
		set_target( setup, OBS_DAC1_HIGH_1XXX, 8.0 );
		setup->n_observables++;
	}

	return 0;
}

static int init_observables_1602_16( calibration_setup_t *setup )
{
	comedi_insn tmpl;//, po_tmpl;
	observable *o;
#if 0
// XXX need to figure out eeprom map
	int retval;
	float target;
	enum source_eeprom_addr
	{
		EEPROM_7V_CHAN = 0x30,
		EEPROM_3500mV_CHAN = 0x32,
		EEPROM_1750mV_CHAN = 0x34,
		EEPROM_875mV_CHAN = 0x36,
		EEPROM_8600uV_CHAN = 0x38,
	};
#endif
	enum calibration_source
	{
		CAL_SRC_GROUND = 0,
		CAL_SRC_7V = 1,
		CAL_SRC_3500mV = 2,
		CAL_SRC_1750mV = 3,
		CAL_SRC_875mV = 4,
		CAL_SRC_minus_10V = 5,
		CAL_SRC_DAC0 = 6,
		CAL_SRC_DAC1 = 7,
	};
#if 0
	memset( &po_tmpl, 0, sizeof(po_tmpl) );
	po_tmpl.insn = INSN_CONFIG;
	po_tmpl.n = 2;
	po_tmpl.subdev = setup->ad_subdev;
#endif
	memset( &tmpl, 0, sizeof(tmpl) );
	tmpl.insn = INSN_READ;
	tmpl.n = 1;
	tmpl.subdev = setup->ad_subdev;

	o = setup->observables + OBS_0V_RANGE_10V_BIP_1602_16;
	o->name = "ground calibration source, 10V bipolar range, ground referenced";
	o->reference_source = CAL_SRC_GROUND;
	o->observe_insn = tmpl;
	o->observe_insn.chanspec = CR_PACK( 0, 0, AREF_GROUND) | CR_ALT_SOURCE | CR_ALT_FILTER;
	o->target = 0.0;

	o = setup->observables + OBS_7V_RANGE_10V_BIP_1602_16;
	o->name = "7V calibration source, 10V bipolar range, ground referenced";
	o->reference_source = CAL_SRC_7V;
	o->observe_insn = tmpl;
	o->observe_insn.chanspec = CR_PACK( 0, 0, AREF_GROUND) | CR_ALT_SOURCE | CR_ALT_FILTER;
	o->target = 7.0;
#if 0
	retval = cb_actual_source_voltage( setup->dev, setup->eeprom_subdev, EEPROM_7V_CHAN, &target );
	if( retval == 0 )
		o->target = target;
#endif
	setup->n_observables = 2;

	return 0;
}

static int cal_cb_pci_1xxx( calibration_setup_t *setup )
{
	enum cal_knobs_1xxx
	{
		DAC0_GAIN_FINE = 0,
		DAC0_GAIN_COARSE,
		DAC0_OFFSET,
		DAC1_OFFSET,
		DAC1_GAIN_FINE,
		DAC1_GAIN_COARSE,
		ADC_OFFSET_COARSE,
		ADC_OFFSET_FINE,
		ADC_GAIN,
	};

	cal1( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_COARSE );
	cal1_fine( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_COARSE );

	cal1( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_FINE );
	cal1_fine( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_FINE );

	cal1( setup, OBS_7V_RANGE_10V_BIP_1XXX, ADC_GAIN );
	cal1_fine( setup, OBS_7V_RANGE_10V_BIP_1XXX, ADC_GAIN );

	if( setup->da_subdev >= 0 )
	{
		cal1( setup, OBS_DAC0_GROUND_1XXX, DAC0_OFFSET );
		cal1( setup, OBS_DAC0_HIGH_1XXX, DAC0_GAIN_COARSE );
		cal1( setup, OBS_DAC0_HIGH_1XXX, DAC0_GAIN_FINE );

		cal1( setup, OBS_DAC1_GROUND_1XXX, DAC1_OFFSET );
		cal1( setup, OBS_DAC1_HIGH_1XXX, DAC1_GAIN_COARSE );
		cal1( setup, OBS_DAC1_HIGH_1XXX, DAC1_GAIN_FINE );
	}

	return 0;
}

static int cal_cb_pci_1001( calibration_setup_t *setup )
{
	enum cal_knobs_1xxx
	{
		DAC0_GAIN_FINE = 0,
		DAC0_GAIN_COARSE,
		DAC0_OFFSET,
		DAC1_OFFSET,
		DAC1_GAIN_FINE,
		DAC1_GAIN_COARSE,
		ADC_OFFSET_COARSE,
		ADC_OFFSET_FINE,
		ADC_GAIN,
	};

	cal1( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_COARSE );
	cal1_fine( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_COARSE );

	cal1( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_FINE );
	cal1_fine( setup, OBS_0V_RANGE_10V_BIP_1XXX, ADC_OFFSET_FINE );

	cal1( setup, OBS_7V_RANGE_10V_BIP_1XXX, ADC_GAIN );
	cal1_fine( setup, OBS_7V_RANGE_10V_BIP_1XXX, ADC_GAIN );

	if( setup->da_subdev >= 0 )
	{
		cal1( setup, OBS_DAC0_GROUND_1XXX, DAC0_OFFSET );
		cal1( setup, OBS_DAC0_HIGH_1XXX, DAC0_GAIN_COARSE );
		cal1( setup, OBS_DAC0_HIGH_1XXX, DAC0_GAIN_FINE );

		cal1( setup, OBS_DAC1_GROUND_1XXX, DAC1_OFFSET );
		cal1( setup, OBS_DAC1_HIGH_1XXX, DAC1_GAIN_COARSE );
		cal1( setup, OBS_DAC1_HIGH_1XXX, DAC1_GAIN_FINE );
	}

	return 0;
}

static int cal_cb_pci_1602_16( calibration_setup_t *setup )
{
	enum cal_knobs_1602_16
	{
		DAC0_GAIN_FINE = 0,
		DAC0_GAIN_COARSE,
		DAC0_OFFSET_COARSE,
		DAC1_OFFSET_COARSE,
		DAC1_GAIN_FINE,
		DAC1_GAIN_COARSE,
		DAC0_OFFSET_FINE,
		DAC1_OFFSET_FINE,
		ADC_GAIN,
		ADC_POSTGAIN_OFFSET,
		ADC_PREGAIN_OFFSET,
	};

	cal1( setup, OBS_0V_RANGE_10V_BIP_1602_16, ADC_PREGAIN_OFFSET );
	cal1_fine( setup, OBS_0V_RANGE_10V_BIP_1602_16, ADC_PREGAIN_OFFSET );

	cal1( setup, OBS_0V_RANGE_10V_BIP_1602_16, ADC_POSTGAIN_OFFSET );
	cal1_fine( setup, OBS_0V_RANGE_10V_BIP_1602_16, ADC_POSTGAIN_OFFSET );

	cal1( setup, OBS_7V_RANGE_10V_BIP_1602_16, ADC_GAIN );
	cal1_fine( setup, OBS_7V_RANGE_10V_BIP_1602_16, ADC_GAIN );

	return 0;
}

// converts calibration source voltages from two 16 bit eeprom values to a floating point value
static float eeprom16_to_source( uint16_t *data )
{
	union translator
	{
		uint32_t bits;
		float	value;
	};

	union translator my_translator;

	my_translator.bits = ( data[ 0 ] & 0xffff ) | ( ( data[ 1 ] << 16 ) & 0xffff0000 );

	return my_translator.value;
}

static float eeprom8_to_source( uint8_t *data )
{
	union translator
	{
		uint32_t bits;
		float	value;
	};
	union translator my_translator;
	int i;

	my_translator.bits = 0;
	for( i = 0; i < 4; i++ )
	{
		my_translator.bits |= ( data[ i ] & 0xffff ) << ( 8 * i );
	}

	return my_translator.value;
}

int cb_actual_source_voltage( comedi_t *dev, unsigned int subdevice, unsigned int eeprom_channel, float *voltage)
{
	int retval;
	unsigned int i;
	lsampl_t data;
	int max_data;

	max_data = comedi_get_maxdata( dev, subdevice, eeprom_channel );

	if( max_data == 0xffff )
	{
		uint16_t word[ 2 ];

		for( i = 0; i < 2; i++ )
		{
			retval = comedi_data_read( dev, subdevice, eeprom_channel + i, 0, 0, &data );
			if( retval < 0 )
			{
				comedi_perror( __FUNCTION__ );
				return retval;
			}
			word[ i ] = data;
		}
		*voltage = eeprom16_to_source( word );
	}else if( max_data == 0xff )
	{
		uint8_t byte[ 4 ];

		for( i = 0; i < 4; i++ )
		{
			retval = comedi_data_read( dev, subdevice, eeprom_channel + i, 0, 0, &data );
			if( retval < 0 )
			{
				comedi_perror( __FUNCTION__ );
				return retval;
			}
			byte[ i ] = data;
		}
		*voltage = eeprom8_to_source( byte );
	}else
	{
		fprintf( stderr, "actual_source_voltage(), maxdata = 0x%x invalid\n", max_data );
		return -1;
	}

	DPRINT(0, "eeprom ch 0x%x gives calibration source of %gV\n", eeprom_channel, *voltage);
	return 0;
}
