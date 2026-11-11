#ifndef PACER_H_INCLUDED
#define PACER_H_INCLUDED

#include "../GlobalDefinitions.h"

/* AetherSDR: one portable implementation (std::chrono, aether/Pacer.cpp);
   no <windows.h> in a header the receiver core includes. */

class CPacer
{
public:
	CPacer(uint64_t ns);
	~CPacer();
	uint64_t nstogo();
	void wait();
protected:
	uint64_t timekeeper;
	uint64_t interval;
};
#endif
