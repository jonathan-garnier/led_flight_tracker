#!/usr/bin/env python3
"""
Test script for OpenSky Network API authentication
Tests both HTTP Basic Auth and query parameter authentication
"""

import requests
import base64
import json
import sys
from requests.auth import HTTPBasicAuth

# Force UTF-8 output on Windows
sys.stdout.reconfigure(encoding='utf-8')

# Configuration - UPDATE THESE WITH YOUR CREDENTIALS
USERNAME = "fructis98-api-client"
PASSWORD = "uuDRTP6XkLECRhOE42r82DQkiGKEBKIj"

# API endpoint
API_URL = "https://opensky-network.org/api/states/all"

# Test bounding box (Sydney area)
LAT_MIN = -34.4500
LON_MIN = 150.6817
LAT_MAX = -33.4500
LON_MAX = 151.6817

def test_without_auth():
    """Test API without authentication"""
    print("\n" + "="*60)
    print("TEST 1: API without authentication")
    print("="*60)

    params = {
        "lamin": LAT_MIN,
        "lomin": LON_MIN,
        "lamax": LAT_MAX,
        "lomax": LON_MAX
    }

    try:
        response = requests.get(API_URL, params=params, timeout=10)
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            num_flights = len(data.get('states', []))
            print(f"✓ SUCCESS: Received {num_flights} flights")
            return True
        else:
            print(f"✗ FAILED: {response.status_code}")
            print(f"Response: {response.text[:200]}")
            return False
    except Exception as e:
        print(f"✗ ERROR: {e}")
        return False


def test_basic_auth():
    """Test API with HTTP Basic Authentication"""
    print("\n" + "="*60)
    print("TEST 2: API with HTTP Basic Authentication")
    print("="*60)

    params = {
        "lamin": LAT_MIN,
        "lomin": LON_MIN,
        "lamax": LAT_MAX,
        "lomax": LON_MAX
    }

    try:
        response = requests.get(
            API_URL,
            params=params,
            auth=HTTPBasicAuth(USERNAME, PASSWORD),
            timeout=10
        )
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            num_flights = len(data.get('states', []))
            print(f"✓ SUCCESS: Received {num_flights} flights with Basic Auth")
            return True
        elif response.status_code == 401:
            print(f"✗ FAILED: 401 Unauthorized")
            print(f"Response: {response.text[:200]}")
            print("\nPossible issues:")
            print("  - Credentials are incorrect")
            print("  - API doesn't support Basic Auth")
            print("  - API key has expired")
            return False
        else:
            print(f"✗ FAILED: {response.status_code}")
            print(f"Response: {response.text[:200]}")
            return False
    except Exception as e:
        print(f"✗ ERROR: {e}")
        return False


def test_query_params_auth():
    """Test API with credentials in query parameters"""
    print("\n" + "="*60)
    print("TEST 3: API with Query Parameter Authentication")
    print("="*60)

    params = {
        "lamin": LAT_MIN,
        "lomin": LON_MIN,
        "lamax": LAT_MAX,
        "lomax": LON_MAX,
        "username": USERNAME,
        "password": PASSWORD
    }

    try:
        response = requests.get(API_URL, params=params, timeout=10)
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            num_flights = len(data.get('states', []))
            print(f"✓ SUCCESS: Received {num_flights} flights with Query Params")
            return True
        elif response.status_code == 401:
            print(f"✗ FAILED: 401 Unauthorized")
            print(f"Response: {response.text[:200]}")
            return False
        else:
            print(f"✗ FAILED: {response.status_code}")
            print(f"Response: {response.text[:200]}")
            return False
    except Exception as e:
        print(f"✗ ERROR: {e}")
        return False


def test_base64_encoding():
    """Verify base64 encoding of credentials"""
    print("\n" + "="*60)
    print("TEST 4: Base64 Encoding Verification")
    print("="*60)

    credentials = f"{USERNAME}:{PASSWORD}"
    encoded = base64.b64encode(credentials.encode()).decode()

    print(f"Username: {USERNAME}")
    print(f"Password: {PASSWORD}")
    print(f"Credentials string: {credentials}")
    print(f"Base64 encoded: {encoded}")
    print(f"Authorization header: Basic {encoded}")
    print(f"✓ Encoding test complete")
    return True


def test_flight_data_detailed():
    """Test API with query params and show all flight data"""
    print("\n" + "="*60)
    print("DETAILED FLIGHT DATA (Query Parameter Auth)")
    print("="*60)

    params = {
        "lamin": LAT_MIN,
        "lomin": LON_MIN,
        "lamax": LAT_MAX,
        "lomax": LON_MAX,
        "username": USERNAME,
        "password": PASSWORD
    }

    try:
        response = requests.get(API_URL, params=params, timeout=10)
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            flights = data.get('states', [])
            print(f"Found {len(flights)} flights\n")

            if len(flights) > 0:
                print("="*120)
                print("FLIGHT DATA BREAKDOWN")
                print("="*120)

                for i, flight in enumerate(flights[:20]):  # Show first 20 flights
                    print(f"\n--- Flight {i+1} ---")

                    # Array indices according to OpenSky API documentation:
                    # 0: icao24, 1: callsign, 2: origin_country, 3: time_position, 4: last_contact
                    # 5: longitude, 6: latitude, 7: baro_altitude, 8: on_ground, 9: velocity
                    # 10: true_track, 11: vertical_rate, 12: sensors, 13: geo_altitude
                    # 14: squawk, 15: spi, 16: position_source, 17: category

                    if len(flight) > 0:
                        print(f"Index 0  - ICAO24: {flight[0]}")
                    if len(flight) > 1:
                        print(f"Index 1  - Callsign: {flight[1]}")
                    if len(flight) > 2:
                        print(f"Index 2  - Country: {flight[2]}")
                    if len(flight) > 5:
                        print(f"Index 5  - Longitude: {flight[5]}")
                    if len(flight) > 6:
                        print(f"Index 6  - Latitude: {flight[6]}")
                    if len(flight) > 7:
                        print(f"Index 7  - Baro Altitude (m): {flight[7]}")
                    if len(flight) > 9:
                        print(f"Index 9  - Velocity (m/s): {flight[9]}")
                    if len(flight) > 10:
                        print(f"Index 10 - True Track (degrees): {flight[10]}")
                    if len(flight) > 13:
                        print(f"Index 13 - Geo Altitude (m): {flight[13]}")
                    if len(flight) > 14:
                        print(f"Index 14 - Squawk: {flight[14]}")
                    if len(flight) > 17:
                        print(f"Index 17 - Category: {flight[17]}")

                    # Print full array for inspection
                    print(f"\nFull array ({len(flight)} elements):")
                    for idx, val in enumerate(flight):
                        print(f"  [{idx}]: {val}")

            return True
        else:
            print(f"✗ FAILED: {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ ERROR: {e}")
        return False


def main():
    print("\n" + "="*60)
    print("OpenSky Network API Authentication Test")
    print("="*60)

    results = {
        "Without Auth": test_without_auth(),
        "Query Params": test_query_params_auth(),
        "Base64 Encoding": test_base64_encoding()
    }

    print("\n" + "="*60)
    print("TEST SUMMARY")
    print("="*60)

    for test_name, result in results.items():
        status = "✓ PASS" if result else "✗ FAIL"
        print(f"{test_name}: {status}")

    print("\n" + "="*60)
    print("INTERPRETATION")
    print("="*60)

    if results["Without Auth"] and results["Query Params"]:
        print("✓ Credentials are valid with query parameters!")
        print("  → Use query parameter authentication")
    elif results["Without Auth"] and not results["Query Params"]:
        print("✗ Credentials are INVALID")
        print("  → Check credentials and re-enter them")
    elif not results["Without Auth"]:
        print("✗ Cannot reach OpenSky API at all")
        print("  → Check your internet connection")

    # Now show detailed flight data
    print("\n")
    test_flight_data_detailed()
    print("\n")


if __name__ == "__main__":
    main()
