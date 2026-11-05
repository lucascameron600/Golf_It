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

#endif
