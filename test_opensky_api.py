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


def main():
    print("\n" + "="*60)
    print("OpenSky Network API Authentication Test")
    print("="*60)

    results = {
        "Without Auth": test_without_auth(),
        "Basic Auth": test_basic_auth(),
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

    if results["Without Auth"] and results["Basic Auth"]:
        print("✓ Credentials are valid! Basic Auth works.")
        print("  → The ESP32 stack overflow is likely due to recursion in the fallback test")
        print("  → Remove the fallback test code from flight_api.cpp")
    elif results["Without Auth"] and results["Query Params"]:
        print("✓ Credentials are valid with query parameters!")
        print("  → Use query parameter authentication instead of Basic Auth")
        print("  → Remove HTTP Basic Auth code, keep only query params")
    elif results["Without Auth"] and not results["Basic Auth"]:
        print("✗ Credentials are INVALID")
        print("  → Check credentials.json and re-enter them")
        print("  → Make sure you copied the API username/password, not account login")
    elif not results["Without Auth"]:
        print("✗ Cannot reach OpenSky API at all")
        print("  → Check your internet connection")
        print("  → Check if opensky-network.org is accessible")

    print("\n")


if __name__ == "__main__":
    main()
