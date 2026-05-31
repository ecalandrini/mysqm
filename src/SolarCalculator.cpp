#include "SolarCalculator.h"

// Helper function to get the day of the year
static int getDayOfYear(int year, int month, int day) {
    static const int daysInMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int n = day;
    for (int i = 1; i < month; i++) {
        n += daysInMonth[i];
    }
    
    // Leap year check
    if (month > 2) {
        bool isLeap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (isLeap) {
            n += 1;
        }
    }
    return n;
}

double SolarCalculator::normalizeAngle(double angle) {
    double normalized = fmod(angle, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    return normalized;
}

bool SolarCalculator::calculateSunriseSunsetUTC(
    float latitude, 
    float longitude, 
    int year, 
    int month, 
    int day, 
    float &sunriseMinutesUTC, 
    float &sunsetMinutesUTC
) {
    // 1. Calculate the day of the year
    int N = getDayOfYear(year, month, day);

    // 2. Longitude to hour value
    double lngHour = longitude / 15.0;

    // Standard horizon zenith angle for rising/setting is 90 degrees 50 minutes (90.833 degrees)
    // This accounts for refraction and the sun's semi-diameter
    const double zenith = 90.83333333333333;

    double times[2] = {0.0, 0.0}; // times[0] = sunrise, times[1] = sunset

    for (int isSet = 0; isSet < 2; isSet++) {
        // 3. Approximate rising or setting time
        double t = N + ((isSet == 0 ? 6.0 : 18.0) - lngHour) / 24.0;

        // 4. Sun's mean anomaly
        double M = 0.9856 * t - 3.289;
        double M_rad = degToRad(normalizeAngle(M));

        // 5. Sun's true longitude
        double L = M + 1.916 * sin(M_rad) + 0.020 * sin(2.0 * M_rad) + 282.634;
        L = normalizeAngle(L);
        double L_rad = degToRad(L);

        // 6. Sun's right ascension
        double RA = radToDeg(atan(0.91746 * tan(L_rad)));
        RA = normalizeAngle(RA);

        // Force RA into the same quadrant as L
        double L_quadrant  = floor(L / 90.0) * 90.0;
        double RA_quadrant = floor(RA / 90.0) * 90.0;
        RA = RA + (L_quadrant - RA_quadrant);

        // Convert RA to hours
        RA = RA / 15.0;

        // 7. Sun's declination
        double sinDec = 0.39782 * sin(L_rad);
        double cosDec = cos(asin(sinDec));

        // 8. Sun's local hour angle
        double lat_rad = degToRad(latitude);
        double cosH = (cos(degToRad(zenith)) - (sinDec * sin(lat_rad))) / (cosDec * cos(lat_rad));

        if (cosH > 1.0) {
            // Sun never rises (polar night)
            return false;
        }
        if (cosH < -1.0) {
            // Sun never sets (polar day)
            return false;
        }

        // Calculate H and convert to hours
        double H;
        if (isSet == 0) {
            H = 360.0 - radToDeg(acos(cosH));
        } else {
            H = radToDeg(acos(cosH));
        }
        H = H / 15.0;

        // 9. Calculate local mean time of rising/setting
        double T = H + RA - (0.06571 * t) - 6.622;

        // 10. Adjust to UTC hours and wrap around 24
        double UTCHour = T - lngHour;
        UTCHour = fmod(UTCHour, 24.0);
        if (UTCHour < 0.0) {
            UTCHour += 24.0;
        }

        times[isSet] = UTCHour * 60.0;
    }

    sunriseMinutesUTC = (float)times[0];
    sunsetMinutesUTC = (float)times[1];
    return true;
}
