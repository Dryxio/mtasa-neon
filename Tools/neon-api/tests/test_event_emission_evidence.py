from __future__ import annotations

import copy
import hashlib
import subprocess
import sys
import tempfile
import unittest
import json
import zipfile
from pathlib import Path
from unittest import mock

TOOL = Path(__file__).resolve().parents[1]
REPOSITORY = TOOL.parents[1]
sys.path.insert(0, str(TOOL))
from neonlib.catalogue import (SourceSnapshot, NEON_REPOSITORY, UPSTREAM_REPOSITORY,
    _event_symbol, _load_emission_evidence, extract_event_registrations, git_snapshot, catalogue_source_matches)


class EventEmissionEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.neon = git_snapshot(REPOSITORY, '27101451cad91104ccf8337b68d55af893a4db48')
        cls.current = git_snapshot(REPOSITORY, 'ebd79fe6cc92a18d23ed3b74753d248521b4b5f9')
        cls.upstream = git_snapshot(REPOSITORY, '4b766ae92db1e7efbcb72b74af4fcbd4c2a17a54')
        cls.evidence = _load_emission_evidence()
        cls.contracts = cls.evidence['snapshots'][0]['events']

    def symbol(self, contract, neon=None, upstream=None, evidence=None, arity=None):
        neon = neon or self.neon
        upstream = upstream or self.upstream
        name = contract['name']
        documentation = {'side': contract['side'], 'parameters': [
            {'name': f'argument{i}', 'type': 'unknown', 'optional': False}
            for i in range(contract['arity'] if arity is None else arity)]}
        return _event_symbol(name,
            [x for x in extract_event_registrations(neon) if x.name == name],
            [x for x in extract_event_registrations(upstream) if x.name == name],
            documentation, None, neon, upstream, self.evidence if evidence is None else evidence)

    def test_seven_reconciliations_on_both_audited_neon_snapshots(self):
        self.assertEqual(len(self.contracts), 7)
        for contract in self.contracts:
            for snapshot in (self.neon, self.current):
                with self.subTest(name=contract['name'], revision=snapshot.revision):
                    symbol = self.symbol(contract, neon=snapshot)
                    self.assertEqual(symbol['state'], 'verified')
                    self.assertEqual(symbol['registrationDifferences'], ['parameter-count'])
                    paths = {p['path'] for p in symbol['provenance']}
                    self.assertTrue({e['path'] for e in contract['emitters']} <= paths)
                    self.assertEqual(self.symbol(contract, evidence={})['state'], 'conflict')

    def test_mutated_missing_or_other_source_invalidates_global_fingerprint(self):
        contract = self.contracts[1]  # Emitter file is separate from registration.
        emitter = contract['emitters'][0]['path']
        other = 'Shared/mods/deathmatch/logic/Enums.cpp'
        for path in (emitter, other):
            for remove in (False, True):
                files = dict(self.neon.files)
                if remove:
                    del files[path]
                else:
                    files[path] += '\n// Changed source requires a new audit.\n'
                changed = SourceSnapshot(self.neon.revision, files, self.neon.engine_source_digest)
                with self.subTest(path=path, remove=remove):
                    self.assertEqual(self.symbol(contract, neon=changed)['state'], 'conflict')
        changed = SourceSnapshot('0' * 40, self.neon.files)
        self.assertEqual(self.symbol(contract, neon=changed)['state'], 'conflict')
        changed = SourceSnapshot(self.upstream.revision, {**self.upstream.files, other: 'changed'})
        self.assertEqual(self.symbol(contract, upstream=changed)['state'], 'conflict')

    def test_malformed_ambiguous_and_wrong_proof_is_not_a_downgrade(self):
        contract = self.contracts[0]
        def check(mutate):
            data = copy.deepcopy(self.evidence)
            mutate(data)
            self.assertEqual(self.symbol(contract, evidence=data)['state'], 'conflict')
        check(lambda d: d['snapshots'].append(copy.deepcopy(d['snapshots'][0])))
        check(lambda d: d['snapshots'][0]['events'].append(copy.deepcopy(d['snapshots'][0]['events'][0])))
        check(lambda d: d['snapshots'][0]['events'][0]['emitters'][0].update(sha256='0' * 64))
        check(lambda d: d['snapshots'][0]['events'][0]['emitters'][0].update(callCount=100))
        check(lambda d: d['snapshots'][0]['events'][0].update(emitters=[]))
        check(lambda d: d['snapshots'][0]['events'][0].update(side='server'))
        check(lambda d: d['snapshots'][0]['events'][0].update(arity=99))
        self.assertEqual(self.symbol(contract, arity=99)['state'], 'conflict')
        with mock.patch('neonlib.jsonio.load_json', side_effect=FileNotFoundError):
            self.assertEqual(_load_emission_evidence(), {})

    def test_real_unreviewed_count_conflict_and_side_conflict_remain(self):
        damage = {'name': 'onClientPlayerDamage', 'side': 'client', 'arity': 4}
        self.assertEqual(self.symbol(damage)['state'], 'conflict')
        wrong_side = {**self.contracts[0], 'side': 'server'}
        self.assertEqual(self.symbol(wrong_side)['state'], 'conflict')

    def test_portable_archive_contains_checked_evidence_and_detects_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'neon.zip'
            result = subprocess.run([sys.executable, str(TOOL / 'packaging/build_portable.py'),
                '--output', str(archive), '--json'], cwd=REPOSITORY, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with zipfile.ZipFile(archive) as package:
                relative = 'neon-cli/Tools/neon-api/event-emission-evidence.json'
                self.assertEqual(package.read(relative), (TOOL / 'event-emission-evidence.json').read_bytes())
                package.extractall(Path(directory) / 'unpacked')
            portable = Path(directory) / 'unpacked/neon-cli'
            cli = portable / 'Tools/neon-api/neon.py'
            def verify():
                process = subprocess.run([sys.executable, str(cli), 'self-test', '--json'],
                    cwd=portable, capture_output=True, text=True)
                return process, json.loads(process.stdout)
            process, result = verify()
            self.assertEqual(process.returncode, 0, result)
            evidence = portable / 'Tools/neon-api/event-emission-evidence.json'
            evidence.write_text(evidence.read_text() + '\n')
            process, result = verify()
            self.assertNotEqual(process.returncode, 0)
            self.assertNotEqual(result['status'], 'pass')

    def test_tooling_only_commit_keeps_source_snapshot_and_proof_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.check_output(['git', '-c', 'user.name=Fixture', '-c',
                    'user.email=fixture@example.invalid', *args], cwd=root, text=True).strip()
            git('init', '-q')
            path = 'Client/mods/deathmatch/logic/CClientGame.cpp'
            source = ('m_Events.AddEvent("onClientPreRender", "", NULL, false);\n'
                      'CLuaArguments args; args.PushNumber(slice);\n'
                      'root->CallEvent("onClientPreRender", args, false);\n')
            target = root / path
            target.parent.mkdir(parents=True)
            target.write_text(source)
            git('add', '.'); git('commit', '-qm', 'source')
            before = git_snapshot(root, 'HEAD')
            (root / 'tooling.txt').write_text('Tooling-only checkpoint')
            git('add', '.'); git('commit', '-qm', 'tooling')
            after = git_snapshot(root, 'HEAD')
            self.assertNotEqual(git('rev-parse', 'HEAD'), before.revision)
            self.assertEqual((before.revision, before.digest), (after.revision, after.digest))
            contract = {'name': 'onClientPreRender', 'side': 'client', 'arity': 1,
                'emitters': [{'path': path, 'sha256': hashlib.sha256(source.encode()).hexdigest(), 'callCount': 1}]}
            data = {'formatVersion': 1, 'snapshots': [{'repository': NEON_REPOSITORY,
                'revision': before.revision, 'sourceDigest': before.digest, 'engineSourceDigest': before.engine_source_digest, 'events': [contract]}]}
            resolved = self.symbol(contract, neon=after, upstream=SourceSnapshot('empty', {}), evidence=data)
            self.assertEqual(resolved['state'], 'verified')
            old_catalogue = {'sources': {'neon': {'registrationDigest': before.digest}}, 'symbols': [resolved]}
            with mock.patch('neonlib.catalogue._load_emission_evidence', return_value=data):
                self.assertTrue(catalogue_source_matches(old_catalogue, after))
            # An emitter outside SOURCE_PREFIXES used to retain the exact same
            # registration snapshot and incorrectly reuse its arity proof.
            extra = root / 'Client/mods/deathmatch/logic/CClientVehicle.cpp'
            extra.write_text('CLuaArguments empty; root->CallEvent("onClientPreRender", empty, false);\n')
            git('add', '.'); git('commit', '-qm', 'new unselected emitter')
            changed = git_snapshot(root, 'HEAD')
            self.assertEqual((changed.revision, changed.digest), (before.revision, before.digest))
            self.assertNotEqual(changed.engine_source_digest, before.engine_source_digest)
            self.assertEqual(self.symbol(contract, neon=changed, upstream=SourceSnapshot('empty', {}), evidence=data)['state'], 'conflict')
            with mock.patch('neonlib.catalogue._load_emission_evidence', return_value=data):
                self.assertFalse(catalogue_source_matches(old_catalogue, changed))
                self.assertFalse(catalogue_source_matches(old_catalogue, SourceSnapshot(before.revision, before.files)))




if __name__ == '__main__':
    unittest.main()
