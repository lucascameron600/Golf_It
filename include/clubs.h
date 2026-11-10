#ifndef CLUBS_H
#define CLUBS_H
#include <string>
#include <vector>

// Pro golfers' yardage for each club, read from data/pro_club_distances.csv
// (open it in Excel to change the numbers or add players).
struct ClubDistance {
    std::string player;  // "Rory McIlroy", "Tour Average", ...
    std::string club;    // "Driver", "7-iron", "PW", ...
    int yards = 0;
};

// Empty list if the file can't be opened.
std::vector<ClubDistance> loadClubDistances(const std::string& path);

// One line per player: "Rory McIlroy: Driver 320  3-wood 297 ..."
void printClubDistances(const std::vector<ClubDistance>& clubs);

// A bag is a player's rows in `clubs`, longest first, with -1 (the putter) last.
// Returns which slot to hit next: the putter when putting, otherwise the club
// whose carry is closest to the distance left to the pin.
int suggestClub(const std::vector<ClubDistance>& clubs, const std::vector<int>& bag,
                float toPinMeters, bool putting);

// How a tour player's ball comes down with a club that carries this far: the
// angle it drops in at, its speed, and the backspin it still has. Long clubs come
// in flat and fast with little spin (and run out), wedges steep and spinning
// (and stop). Worked out from the carry so it fits any player in the CSV.
struct ClubLanding {
    float angleDeg;  // below level
    float speed;     // m/s
    float spinRpm;   // backspin left at landing
};
ClubLanding clubLanding(float carryYards);

#endif
