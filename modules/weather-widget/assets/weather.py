#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded HTTPS weather requests, cached outside the desktop process."""
import configparser
import hashlib
import io
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
from urllib.parse import urlencode
from urllib.request import Request, urlopen


def request(base, params):
    req = Request(base + '?' + urlencode(params), headers={'User-Agent': 'XFCE-Tweaks-Weather/0.1 (desktop widget)'})
    with urlopen(req, timeout=12) as response:
        raw = response.read(1024 * 1024 + 1)
    if len(raw) > 1024 * 1024:
        raise ValueError('Response exceeds limit')
    return json.loads(raw)


def search(query):
    data = request('https://geocoding-api.open-meteo.com/v1/search', {'name': query.strip()[:160], 'count': 10, 'language': 'en', 'format': 'json'})
    return data.get('results', [])


def condition(code, day=True):
    if code == 0: return ('Clear sky', 'weather-clear-symbolic' if day else 'weather-clear-night-symbolic')
    if code in (1, 2): return ('Partly cloudy', 'weather-few-clouds-symbolic')
    if code == 3: return ('Overcast', 'weather-overcast-symbolic')
    if code in (45, 48): return ('Fog', 'weather-fog-symbolic')
    if code in (51, 53, 55, 56, 57): return ('Drizzle', 'weather-showers-scattered-symbolic')
    if code in (61, 63, 65, 66, 67, 80, 81, 82): return ('Rain', 'weather-showers-symbolic')
    if code in (71, 73, 75, 77, 85, 86): return ('Snow', 'weather-snow-symbolic')
    if code in (95, 96, 99): return ('Thunderstorm', 'weather-storm-symbolic')
    return ('Weather unavailable', 'weather-overcast-symbolic')


def finite(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Missing or invalid weather value')
    return value


def format_weather(data, units):
    current = data['current']; daily = data['daily']
    unit = '°F' if units == 'fahrenheit' else '°C'
    desc, icon = condition(current['weather_code'], current.get('is_day', 1))
    result = {
        'temperature': f"{finite(current['temperature_2m']):.0f}{unit}",
        'condition': desc, 'icon': icon,
        'detail': f"Feels {finite(current['apparent_temperature']):.0f}{unit}  ·  Humidity {finite(current['relative_humidity_2m']):.0f}%  ·  Wind {finite(current['wind_speed_10m']):.0f} {'mph' if units == 'fahrenheit' else 'km/h'}",
        'forecast': '   '.join(f"{day[5:]}  {finite(low):.0f}–{finite(high):.0f}{unit}" for day, low, high in zip(daily['time'][:3], daily['temperature_2m_min'][:3], daily['temperature_2m_max'][:3])),
        'status': 'Open-Meteo · Updated ' + time.strftime('%H:%M'),
    }
    return result


def weather(latitude, longitude, units):
    latitude, longitude = float(latitude), float(longitude)
    if not (math.isfinite(latitude) and math.isfinite(longitude) and -90 <= latitude <= 90 and -180 <= longitude <= 180):
        raise ValueError('Invalid coordinates')
    if units not in ('celsius', 'fahrenheit'): raise ValueError('Invalid units')
    key = hashlib.sha256(f'{latitude}/{longitude}/{units}'.encode()).hexdigest()
    cache = Path(os.environ.get('XDG_CACHE_HOME', Path.home()/'.cache')) / 'xfce-tweaks/weather' / (key + '.json')
    old = None
    try:
        old = json.loads(cache.read_text())
        if 0 <= time.time() - old['timestamp'] < 900: return old['weather']
    except (OSError, ValueError, KeyError, TypeError): pass
    try:
        data = request('https://api.open-meteo.com/v1/forecast', {
            'latitude': latitude, 'longitude': longitude,
            'current': 'temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m',
            'daily': 'temperature_2m_max,temperature_2m_min', 'forecast_days': 3,
            'timezone': 'auto', 'temperature_unit': units,
            'wind_speed_unit': 'mph' if units == 'fahrenheit' else 'kmh',
        })
        result = format_weather(data, units)
        cache.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=cache.parent, prefix='.weather-')
        try:
            with os.fdopen(fd, 'w') as stream:
                json.dump({'timestamp': time.time(), 'weather': result}, stream); stream.flush(); os.fsync(stream.fileno())
            os.replace(tmp, cache)
        finally:
            if os.path.exists(tmp): os.unlink(tmp)
        return result
    except Exception:
        if isinstance(old, dict) and isinstance(old.get('weather'), dict):
            result = dict(old['weather']); result['status'] = 'Offline · cached weather (may be outdated)'; return result
        return {'temperature': '—', 'condition': 'Weather unavailable', 'status': 'Network error · retries in 15 min', 'icon': 'weather-overcast-symbolic'}


if __name__ == '__main__':
    try: result = weather(*sys.argv[1:4])
    except Exception: result = {'condition': 'Invalid weather configuration', 'status': 'Click to configure'}
    config = configparser.ConfigParser(interpolation=None); config.optionxform = str
    config['Weather'] = result
    output = io.StringIO(); config.write(output, space_around_delimiters=False); print(output.getvalue(), end='')
