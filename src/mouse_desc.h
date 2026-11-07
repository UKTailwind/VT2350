/* mouse_desc.h: from the PicoMite (see mouse_desc.c). */
#ifndef MOUSE_DESC_H
#define MOUSE_DESC_H
#include <stdbool.h>
#include <stdint.h>

// Mouse report type enumeration
typedef enum
{
	MOUSE_TYPE_UNKNOWN = 0,
	MOUSE_TYPE_STANDARD_8BIT = 1, // 4 bytes: buttons, X, Y, wheel
	MOUSE_TYPE_HIGHRES_12BIT = 2, // 5 bytes: buttons, 12-bit X/Y packed, wheel
	MOUSE_TYPE_GAMING_16BIT = 3	  // 6+ bytes: buttons, 16-bit X, 16-bit Y, wheel, etc.
} mouse_report_type_t;

/* One Input field of the mouse report, captured from the report descriptor
   at enumeration. bit_offset is within the report payload, excluding any
   report-ID byte. */
typedef struct
{
	bool present;
	uint16_t bit_offset;
	uint8_t bit_size;
	bool is_signed;	   /* Logical Minimum < 0 on the declaring Input item */
	uint8_t report_id; /* report this field belongs to (0 = no IDs used) */
} mouse_field_t;

// Structure to hold mouse analysis results
typedef struct
{
	mouse_report_type_t type;
	uint8_t report_length;
	uint8_t x_bits;
	uint8_t y_bits;
	uint8_t button_count;
	bool has_wheel;
	bool has_pan;
	uint8_t wheel_byte_offset;
	bool uses_report_id; // <-- Add this
	uint8_t report_id;	 // <-- Add this
	/* General descriptor-driven layout: process_mouse_report() decodes by
	   bit extraction from these fields, so any single-pointer layout works
	   (arbitrary padding, field order, button counts, X/Y widths, report
	   IDs) — not just the three fixed types above, which are kept only as
	   a summary for print_mouse_info(). */
	bool layout_valid; /* X and Y found in one report */
	uint8_t min_len;   /* min payload bytes (excl. ID byte) to decode */
	mouse_field_t f_buttons; /* run of 1-bit buttons as one bitmap */
	mouse_field_t f_x, f_y, f_wheel, f_pan;
} mouse_info_t;

mouse_report_type_t analyze_mouse_descriptor(const uint8_t *desc_report, uint16_t desc_len, mouse_info_t *info);
int mouse_field_value(const uint8_t *p, const mouse_field_t *f);
#endif
