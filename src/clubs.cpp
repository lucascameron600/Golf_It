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

// Three anchor clubs, in between is a straight line:
//   land angle: TrackMan PGA Tour averages (driver 38, 5-iron 49, PW 52 deg)
//   speed and backspin at landing: measured landings in golf ball bounce research
//   (driver ~29.5 m/s with ~1950 rpm, 5-iron ~25 m/s with ~4440 rpm,
//    wedge ~22 m/s with ~9000 rpm; spin drops by a fifth or more in the air)
ClubLanding clubLanding(float carryYards) {
    struct Anchor { float yards; ClubLanding land; };
    static const Anchor anchors[] = {
        { 100.0f, { 52.0f, 22.0f, 9000.0f } },  // wedge
        { 194.0f, { 49.0f, 25.0f, 4440.0f } },  // 5-iron
        { 275.0f, { 38.0f, 29.5f, 1950.0f } },  // driver
    };
    if (carryYards <= anchors[0].yards) return anchors[0].land;
    for (int i = 0; i + 1 < 3; i++) {
        const Anchor& a = anchors[i];
        const Anchor& b = anchors[i + 1];
        if (carryYards <= b.yards) {
            float t = (carryYards - a.yards) / (b.yards - a.yards);
            return { a.land.angleDeg + (b.land.angleDeg - a.land.angleDeg) * t,
                     a.land.speed    + (b.land.speed    - a.land.speed)    * t,
                     a.land.spinRpm  + (b.land.spinRpm  - a.land.spinRpm)  * t };
        }
    }
    return anchors[2].land;  // longer than a tour driver: lands like one
}
