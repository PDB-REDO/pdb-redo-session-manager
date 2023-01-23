/*
   Created by: Maarten L. Hekkelman
   Date: dinsdag 19 juni, 2018
*/

#include "ramachandran.hpp"

#include <cif++.hpp>

#include <cassert>
#include <cmath>

#include <map>
#include <mutex>

#include <clipper/clipper.h>

// --------------------------------------------------------------------

using namespace std;

// --------------------------------------------------------------------

class RamachandranTables
{
  public:
	static RamachandranTables &instance()
	{
		lock_guard<mutex> lock(sMutex);

		static RamachandranTables sInstance;
		return sInstance;
	}

	clipper::Ramachandran &table(const string &aa, bool prePro)
	{
		lock_guard<mutex> lock(sMutex);

		auto i = mTables.find(make_tuple(aa, prePro));
		if (i == mTables.end())
		{
			clipper::Ramachandran::TYPE type;

			if (aa == "GLY")
				type = clipper::Ramachandran::Gly2;
			else if (aa == "PRO")
				type = clipper::Ramachandran::Pro2;
			else if (aa == "ILE" or aa == "VAL")
				type = clipper::Ramachandran::IleVal2;
			else if (prePro)
				type = clipper::Ramachandran::PrePro2;
			else
				type = clipper::Ramachandran::NoGPIVpreP2;

			i = mTables.emplace(make_pair(make_tuple(aa, prePro), clipper::Ramachandran(type))).first;
		}

		return i->second;
	}

  private:
	map<tuple<string, int>, clipper::Ramachandran> mTables;
	static mutex sMutex;
};

mutex RamachandranTables::sMutex;

float calculateRamachandranZScore(const string &aa, bool prePro, float phi, float psi)
{
	auto &table = RamachandranTables::instance().table(aa, prePro);
	return table.probability(phi * cif::kPI / 180, psi * cif::kPI / 180);
}

RamachandranScore calculateRamachandranScore(const string &aa, bool prePro, float phi, float psi)
{
	auto &table = RamachandranTables::instance().table(aa, prePro);

	phi *= cif::kPI / 180;
	psi *= cif::kPI / 180;

	RamachandranScore result;

	if (table.favored(phi, psi))
		result = rsFavoured;
	else if (table.allowed(phi, psi))
		result = rsAllowed;
	else
		result = rsNotAllowed;

	return result;
}
