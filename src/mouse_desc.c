/* mouse_desc.c: the mouse report descriptor parser, from the PicoMite's input/USBKeyboard.c by tools/make_mouse_desc.py. MIT License. */
#include <string.h>
#include "mouse_desc.h"

#define MOUSE_MAX_USAGES 8
#define MOUSE_MAX_REPORT_IDS 8

/* Per-report-ID Input bit counts: each Report ID opens its own bit space. */
typedef struct
{
	uint8_t id;
	uint16_t bits;
} mouse_rbits_t;

static uint16_t mouse_rbits_get(const mouse_rbits_t *tab, uint8_t n, uint8_t id)
{
	for (uint8_t k = 0; k < n; k++)
		if (tab[k].id == id)
			return tab[k].bits;
	return 0;
}

static void mouse_rbits_set(mouse_rbits_t *tab, uint8_t *n, uint8_t id, uint16_t bits)
{
	for (uint8_t k = 0; k < *n; k++)
		if (tab[k].id == id)
		{
			tab[k].bits = bits;
			return;
		}
	if (*n < MOUSE_MAX_REPORT_IDS)
	{
		tab[*n].id = id;
		tab[*n].bits = bits;
		(*n)++;
	}
}

static void mouse_capture_field(mouse_field_t *f, uint16_t off, uint8_t bits, bool is_signed, uint8_t report_id)
{
	if (!f->present)
	{
		f->present = true;
		f->bit_offset = off;
		f->bit_size = bits;
		f->is_signed = is_signed;
		f->report_id = report_id;
	}
}

/* Bit-extract a field from a report payload (little-endian bit stream:
   bit n lives in byte n/8, bit n%8), sign-extending signed fields. */
int mouse_field_value(const uint8_t *p, const mouse_field_t *f)
{
	if (!f->present)
		return 0;
	uint32_t v = 0;
	for (uint8_t i = 0; i < f->bit_size; i++)
	{
		uint16_t b = f->bit_offset + i;
		if (p[b >> 3] & (1u << (b & 7)))
			v |= 1u << i;
	}
	if (f->is_signed && f->bit_size < 32 && (v & (1u << (f->bit_size - 1))))
		v |= ~((1u << f->bit_size) - 1);
	return (int)(int32_t)v;
}

/**
 * Analyze HID report descriptor and capture the mouse report layout
 *
 * @param desc_report Pointer to HID report descriptor
 * @param desc_len Length of descriptor in bytes
 * @param info Pointer to mouse_info_t structure to fill
 * @return mouse_report_type_t summary classification (decode no longer
 *         depends on it — MOUSE_TYPE_UNKNOWN with layout_valid set still
 *         decodes correctly via the field table)
 */
mouse_report_type_t analyze_mouse_descriptor(const uint8_t *desc_report, uint16_t desc_len, mouse_info_t *info)
{
	if (!info)
		return MOUSE_TYPE_UNKNOWN;
	memset(info, 0, sizeof(mouse_info_t));
	if (!desc_report || desc_len == 0)
		return MOUSE_TYPE_UNKNOWN;

	/* Global item state. */
	uint16_t usage_page = 0;
	int32_t logical_min = 0;
	uint8_t report_size = 0, report_count = 0;
	uint8_t cur_id = 0;
	bool saw_report_id = false;
	/* Local item state (cleared after every Main item). */
	uint32_t usages[MOUSE_MAX_USAGES]; /* page << 16 | usage */
	uint8_t n_usages = 0;
	uint32_t usage_min = 0;
	bool have_usage_range = false;
	/* Input bit position within the current report ID's report. */
	uint16_t bit_pos = 0;
	mouse_rbits_t rbits[MOUSE_MAX_REPORT_IDS];
	uint8_t n_rbits = 0;

	for (uint16_t i = 0; i < desc_len;)
	{
		uint8_t prefix = desc_report[i++];
		if (prefix == 0xFE)
		{ /* long item: bDataSize follows, then tag + data */
			if (i < desc_len)
				i += 2 + desc_report[i];
			continue;
		}
		uint8_t bSize = prefix & 0x03;
		if (bSize == 3)
			bSize = 4; /* HID spec: size code 3 means 4 bytes */
		uint8_t bType = (prefix >> 2) & 0x03;
		uint8_t bTag = (prefix >> 4) & 0x0F;
		uint32_t data = 0;
		for (uint8_t j = 0; j < bSize && i + j < desc_len; j++)
			data |= (uint32_t)desc_report[i + j] << (j * 8);
		int32_t sdata = (int32_t)data;
		if (bSize == 1)
			sdata = (int8_t)data;
		else if (bSize == 2)
			sdata = (int16_t)data;
		i += bSize;

		if (bType == 1)
		{ /* Global */
			switch (bTag)
			{
			case 0:
				usage_page = data;
				break;
			case 1:
				logical_min = sdata;
				break;
			case 7:
				report_size = data;
				break;
			case 8: /* Report ID: switch to that report's own bit space */
				mouse_rbits_set(rbits, &n_rbits, cur_id, bit_pos);
				saw_report_id = true;
				cur_id = data;
				bit_pos = mouse_rbits_get(rbits, n_rbits, cur_id);
				break;
			case 9:
				report_count = data;
				break;
			default:
				break;
			}
		}
		else if (bType == 2)
		{ /* Local */
			switch (bTag)
			{
			case 0: /* Usage; 4-byte form carries the page in the top half */
				if (n_usages < MOUSE_MAX_USAGES)
					usages[n_usages++] = (bSize == 4)
											 ? data
											 : (((uint32_t)usage_page << 16) | data);
				break;
			case 1:
				usage_min = data;
				have_usage_range = true;
				break;
			default:
				break;
			}
		}
		else if (bType == 0)
		{ /* Main */
			if (bTag == 8)
			{ /* Input */
				bool constant = data & 0x01;
				if (!constant && report_size && report_count)
				{
					for (uint16_t f = 0; f < report_count; f++)
					{
						/* Usage for field f: queued usages distribute in
						   order (last repeats, per spec); a Usage Min/Max
						   range enumerates from its minimum. */
						uint32_t u = 0;
						if (n_usages)
							u = usages[f < n_usages ? f : n_usages - 1];
						else if (have_usage_range)
							u = ((uint32_t)usage_page << 16) | (usage_min + f);
						uint16_t page = u >> 16;
						uint16_t usage = u & 0xFFFF;
						uint16_t off = bit_pos + f * report_size;
						bool sgn = logical_min < 0;
						if (page == 0x01)
						{ /* Generic Desktop */
							if (usage == 0x30)
								mouse_capture_field(&info->f_x, off, report_size, sgn, cur_id);
							else if (usage == 0x31)
								mouse_capture_field(&info->f_y, off, report_size, sgn, cur_id);
							else if (usage == 0x38)
								mouse_capture_field(&info->f_wheel, off, report_size, sgn, cur_id);
						}
						else if (page == 0x09)
						{ /* Buttons: fold the run of 1-bit fields into one
							 little-endian bitmap, first button = bit 0 */
							if (!info->f_buttons.present && report_size == 1)
							{
								uint16_t nb = report_count - f;
								mouse_capture_field(&info->f_buttons, off,
													nb > 8 ? 8 : (uint8_t)nb, false, cur_id);
							}
						}
						else if (page == 0x0C && usage == 0x0238)
						{ /* Consumer AC Pan */
							mouse_capture_field(&info->f_pan, off, report_size, sgn, cur_id);
						}
					}
				}
				bit_pos += (uint16_t)report_size * report_count;
			}
			/* Local items only apply to the next Main item. */
			n_usages = 0;
			usage_min = 0;
			have_usage_range = false;
		}
	}
	mouse_rbits_set(rbits, &n_rbits, cur_id, bit_pos);

	/* X and Y must live in the same report for a usable pointer layout. */
	if (info->f_x.present && info->f_y.present &&
		info->f_y.report_id == info->f_x.report_id)
	{
		info->layout_valid = true;
		info->uses_report_id = saw_report_id;
		info->report_id = info->f_x.report_id;
		/* Fields under a different report ID arrive in different reports. */
		if (info->f_wheel.present && info->f_wheel.report_id != info->report_id)
			info->f_wheel.present = false;
		if (info->f_pan.present && info->f_pan.report_id != info->report_id)
			info->f_pan.present = false;
		if (info->f_buttons.present && info->f_buttons.report_id != info->report_id)
			info->f_buttons.present = false;

		uint16_t need = 0;
		const mouse_field_t *fields[5] = {&info->f_buttons, &info->f_x,
										  &info->f_y, &info->f_wheel, &info->f_pan};
		for (int k = 0; k < 5; k++)
			if (fields[k]->present)
			{
				uint16_t end = fields[k]->bit_offset + fields[k]->bit_size;
				if (end > need)
					need = end;
			}
		info->min_len = (need + 7) / 8;
	}

	/* Legacy summary fields (print_mouse_info + compatibility). */
	info->x_bits = info->f_x.bit_size;
	info->y_bits = info->f_y.bit_size;
	info->button_count = info->f_buttons.bit_size;
	info->has_wheel = info->f_wheel.present;
	info->wheel_byte_offset = info->f_wheel.bit_offset / 8;
	info->has_pan = info->f_pan.present;
	info->report_length =
		(mouse_rbits_get(rbits, n_rbits, info->report_id) + 7) / 8;
	if (info->uses_report_id)
		info->report_length += 1;

	if (info->x_bits == 8 && info->y_bits == 8)
		info->type = MOUSE_TYPE_STANDARD_8BIT;
	else if (info->x_bits == 12 && info->y_bits == 12)
		info->type = MOUSE_TYPE_HIGHRES_12BIT;
	else if (info->x_bits == 16 && info->y_bits == 16)
		info->type = MOUSE_TYPE_GAMING_16BIT;
	else
		info->type = MOUSE_TYPE_UNKNOWN;
	return info->type;
}
