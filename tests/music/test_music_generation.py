import importlib.util,json,struct,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('generation',ROOT/'scripts/music_generation.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)

class Generation(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
  raw=bytearray(84);raw[:7]=b'\x7fELF\x01\x01\x01';struct.pack_into('<H',raw,18,40)
  struct.pack_into('<I',raw,28,52);struct.pack_into('<HH',raw,42,32,1);struct.pack_into('<I',raw,52,1)
  self.artifacts={}
  for name in g.ROLES:
   p=self.root/name;p.write_bytes(raw);self.artifacts[name]=p
  self.path=self.root/'generation';self.receipt=g.build(self.path,self.artifacts,'fab4-armv6','fixture-revision','fixture-build','a'*64,'b'*64)
  self.id=self.receipt['generation']
 def tearDown(self):self.tmp.cleanup()
 def test_generation_and_expected_paths(self):
  self.assertEqual(g.verify(self.path,self.id),self.receipt)
  self.assertFalse(self.receipt['activation_qualified'])
 def test_repeated_generation_does_not_overwrite(self):
  with self.assertRaises(FileExistsError):g.build(self.path,self.artifacts,'fab4-armv6','revision','build','a'*64,'b'*64)
 def test_missing_and_corrupted_artifacts(self):
  p=self.path/'sbbase-music';p.write_bytes(b'foreign')
  with self.assertRaises(ValueError):g.verify(self.path,self.id)
  p.unlink()
  with self.assertRaises(ValueError):g.verify(self.path,self.id)
 def test_missing_receipt(self):
  (self.path/'receipt.json').unlink()
  with self.assertRaises(FileNotFoundError):g.verify(self.path,self.id)
 def test_stale_source_and_wrong_generation(self):
  with self.assertRaises(ValueError):g.verify(self.path,'c'*64)
  with self.assertRaises(ValueError):g.verify(self.path,self.id,{})
 def test_receipt_path_and_metadata_tampering(self):
  p=self.path/'receipt.json';body=json.loads(p.read_text());body['executable_paths']['sbbase-music']='/bin/unknown';p.write_text(json.dumps(body))
  with self.assertRaises(ValueError):g.verify(self.path,self.id)
 def test_rollback_hash_and_payload(self):
  digest=g.sha((self.path/'receipt.json').read_bytes());g.verify_rollback(self.path,self.id,digest)
  with self.assertRaises(ValueError):g.verify_rollback(self.path,self.id,'0'*64)
  (self.path/'sbwebserver-music').write_bytes(b'broken')
  with self.assertRaises(ValueError):g.verify_rollback(self.path,self.id,digest)
 def test_missing_rollback_artifact(self):
  with self.assertRaises(FileNotFoundError):g.verify_rollback(self.root/'missing',self.id,'a'*64)
 def test_interrupted_new_generation_preserves_previous(self):
  original=(self.path/'receipt.json').read_bytes()
  (self.artifacts['sbmusic-session']).unlink()
  with self.assertRaises(FileNotFoundError):g.build(self.root/'next',self.artifacts,'fab4-armv6','revision','build','a'*64,'b'*64)
  self.assertEqual((self.path/'receipt.json').read_bytes(),original)
  g.verify(self.path,self.id)

if __name__=='__main__':unittest.main()
