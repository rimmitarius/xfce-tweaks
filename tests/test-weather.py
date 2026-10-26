#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import os
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('weather',ROOT/'modules/weather-widget/assets/weather.py');weather=importlib.util.module_from_spec(spec);spec.loader.exec_module(weather)
DATA={'current':{'temperature_2m':12.5,'apparent_temperature':10,'relative_humidity_2m':54,'wind_speed_10m':14,'weather_code':61,'is_day':1},'daily':{'time':['2026-10-20','2026-10-21','2026-10-22'],'temperature_2m_min':[5,6,7],'temperature_2m_max':[15,16,17]}}
class Weather(unittest.TestCase):
 def test_conditions(self):
  for code in [0,1,3,45,51,61,71,95,999]:self.assertEqual(len(weather.condition(code)),2)
  self.assertIn('night',weather.condition(0,False)[1])
 def test_format(self):
  result=weather.format_weather(DATA,'celsius');self.assertIn('°C',result['temperature']);self.assertEqual(result['condition'],'Rain');self.assertIn('10-22',result['forecast'])
 def test_invalid(self):
  for value in [None,float('nan'),float('inf'),True]:
   with self.assertRaises(ValueError):weather.finite(value)
  with self.assertRaises(ValueError):weather.weather('1000','0','celsius')
 def test_cache_and_offline(self):
  with tempfile.TemporaryDirectory() as cache,patch.dict(os.environ,{'XDG_CACHE_HOME':cache}):
   with patch.object(weather,'request',return_value=DATA) as request:
    first=weather.weather(10,20,'celsius');self.assertEqual(first,weather.weather(10,20,'celsius'));self.assertEqual(request.call_count,1)
   with patch.object(weather.time,'time',return_value=99999999999),patch.object(weather,'request',side_effect=OSError()):self.assertIn('cached',weather.weather(10,20,'celsius')['status'])
   with patch.object(weather,'request',side_effect=OSError()):self.assertIn('Network error',weather.weather(11,21,'celsius')['status'])
 def test_shared_exports(self):
  self.assertEqual((ROOT/'modules/common/widget-transfer.h').read_bytes(),(ROOT/'modules/victus-control/widget-transfer.h').read_bytes())
  for ident in ['clock-widget','weather-widget']:
   for source,target in [('widget.h','widget.h'),('content.h','content.h'),('widget-transfer.h','widget-transfer.h'),('settings.py','assets/app.py')]:self.assertEqual((ROOT/'modules/common'/source).read_bytes(),(ROOT/'modules'/ident/target).read_bytes())
if __name__=='__main__':unittest.main()
