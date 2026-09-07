"""Exercise the actual C++ send predicates without building/running the client.

This checks transition policy, not serialization, packet delivery or native GTA.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'Client/mods/deathmatch/logic/CUnoccupiedVehicleSync.cpp'
SERVER = ROOT / 'Server/mods/deathmatch/logic/CUnoccupiedVehicleSync.cpp'


def predicates():
    source = CLIENT.read_text()
    marker = 'if (fabs(vehicle.data.vecVelocity.fX) > FLOAT_EPSILON'
    start = 0
    result = []
    while (start := source.find(marker, start)) != -1:
        begin = source.index('(', start)
        depth = 1
        end = begin + 1
        while depth:
            depth += (source[end] == '(') - (source[end] == ')')
            end += 1
        expression = source[begin + 1:end - 1]
        for index, axis in enumerate('XYZ'):
            expression = expression.replace(f'vehicle.data.vecVelocity.f{axis}', f'current[{index}]')
            expression = expression.replace(f'pVehicle->m_LastSyncedData->vecMoveSpeed.f{axis}', f'previous[{index}]')
        expression = re.sub(r'(\d+\.\d+)f\b', r'\1', expression)
        expression = ' '.join(expression.replace('||', ' or ').split())
        result.append(expression)
        start = end
    return result


class StopTransition(unittest.TestCase):
    def test_boats_and_road_vehicles(self):
        expressions = predicates()
        self.assertEqual(len(expressions), 2)
        cases = [
            ((0, 0, 0), (0, 0, 0), False),
            ((0.2, 0, 0), (0, 0, 0), True),
            ((0, 0, 0), (0.2, 0, 0), True),
            ((0, 0, 0), (0, -0.2, 0), True),
            ((0, 0, 0), (0, 0, -0.2), True),
            ((0.2, 0, 0), (0.2, 0, 0), True),
            ((0.00001, 0, 0.01), (0, 0, 0), False),
            ((0.00001, 0, 0.01), (0.2, 0, 0), True),
        ]
        for expression in expressions:
            for current, previous, expected in cases:
                with self.subTest(current=current, previous=previous):
                    self.assertEqual(eval(expression, {'__builtins__': {}}, dict(
                        fabs=abs, FLOAT_EPSILON=0.0001, current=current, previous=previous)), expected)

    def test_server_suppresses_duplicates_not_stops(self):
        source = SERVER.read_text()
        begin = source.index('if (vehicle.data.bSyncVelocity)', source.index('// Apply the data to the vehicle'))
        end = source.index('if (vehicle.data.bSyncTurnVelocity)', begin)
        block = source[begin:end]
        self.assertIn('if (pVehicle->GetVelocity() == vehicle.data.vecVelocity)', block)
        self.assertIn('pVehicle->SetVelocity(vehicle.data.vecVelocity)', block)
        self.assertNotIn('FLOAT_EPSILON', block)


if __name__ == '__main__':
    unittest.main()
