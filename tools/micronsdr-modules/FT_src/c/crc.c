/* CRC-14 / 13 / 10 of WSJT-X (lib/crc14.cpp, crc13.cpp, crc10.cpp used boost::augmented_crc):
   the CRC is in the last bits of the data (set to zero for the computation); bits MSB first,
   no reflection, initial remainder 0 - the plain polynomial division of the data. */
#include <stdbool.h>

static unsigned augmented(const unsigned char *data, int length, int width, unsigned poly)
{
    const unsigned top = 1u << (width - 1), mask = (1u << width) - 1;
    unsigned r = 0;
    for(int i = 0; i < length; i++)
        for(int b = 7; b >= 0; b--)
        {
            const unsigned q = r & top;
            r = ((r << 1) | ((data[i] >> b) & 1u)) & mask;
            if(q)
                r ^= poly;
        }
    return r & mask;
}

short crc14(const unsigned char *data, int length) { return (short)augmented(data, length, 14, 0x2757); }
bool crc14_check(const unsigned char *data, int length) { return augmented(data, length, 14, 0x2757) == 0; }
short crc13(const unsigned char *data, int length) { return (short)augmented(data, length, 13, 0x15D7); }
bool crc13_check(const unsigned char *data, int length) { return augmented(data, length, 13, 0x15D7) == 0; }
short crc10(const unsigned char *data, int length) { return (short)augmented(data, length, 10, 0x08F); }
bool crc10_check(const unsigned char *data, int length) { return augmented(data, length, 10, 0x08F) == 0; }
