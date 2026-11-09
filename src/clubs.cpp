#include "clubs.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>

std::vector<ClubDistance> loadClubDistances(const std::string& path) {
    std::vector<ClubDistance> out;
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);  // skip the header row
    while (std::getline(file, line)) {
        // only the first three columns are used: player, club, yards
        std::stringstream cols(line);
        ClubDistance d;
        std::string yards;
        if (!std::getline(cols, d.player, ',') || !std::getline(cols, d.club, ',') || !std::getline(cols, yards, ','))
            continue;  // blank or short row
        d.yards = std::atoi(yards.c_str());
        out.push_back(d);
    }
    return out;
}

void printClubDistances(const std::vector<ClubDistance>& clubs) {
    std::printf("Loaded %zu club yardages\n", clubs.size());
    for (size_t i = 0; i < clubs.size(); i++) {
        // rows for the same player are next to each other in the file
        if (i == 0 || clubs[i].player != clubs[i - 1].player)
            std::printf("%s%s:", i == 0 ? "" : "\n", clubs[i].player.c_str());
        std::printf("  %s %d", clubs[i].club.c_str(), clubs[i].yards);
    }
    if (!clubs.empty()) std::printf("\n");
}

int suggestClub(const std::vector<ClubDistance>& clubs, const std::vector<int>& bag,
                float toPinMeters, bool putting) {
    int best = (int)bag.size() - 1;  // the putter
    if (putting) return best;
    float bestGap = 1e9f;
    for (int s = 0; s < (int)bag.size(); s++) {
        if (bag[s] < 0) continue;
        float gap = std::fabs(clubs[bag[s]].yards * 0.9144f - toPinMeters);  // yards -> meters
        if (gap < bestGap) { bestGap = gap; best = s; }
    }
    return best;
}
