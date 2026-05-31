#ifndef SOLAR_CALCULATOR_H
#define SOLAR_CALCULATOR_H

#include <Arduino.h>
#include <math.h>

class SolarCalculator {
public:
    // Calculates sunrise and sunset in minutes from UTC midnight
    // latitude: North is positive, South is negative (degrees)
    // longitude: East is positive, West is negative (degrees)
    // year, month, day: UTC calendar date
    // returns true on success, false if the sun never rises or never sets (polar day/night)
    static bool calculateSunriseSunsetUTC(
        float latitude, 
        float longitude, 
        int year, 
        int month, 
        int day, 
        float &sunriseMinutesUTC, 
        float &sunsetMinutesUTC
    );

private:
    // Convert degrees to radians
    static inline double degToRad(double deg) { return deg * M_PI / 180.0; }

    // Convert radians to degrees
    static inline double radToDeg(double rad) { return rad * 180.0 / M_PI; }

    // Normalizes an angle to range [0, 360)
    static double normalizeAngle(double angle);
};

#endif // SOLAR_CALCULATOR_H
