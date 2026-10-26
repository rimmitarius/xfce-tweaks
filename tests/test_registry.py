# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import configparser,os,sys,tempfile,unittest,zipfile
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'manager'))
from registry import import_package,discover,metadata,validate_elf
class Packages(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.env=patch.dict(os.environ,{'XDG_DATA_HOME':str(self.root/'data')});self.env.start()
        self.modules=self.root/'data/xfce-tweaks/modules'
        self.ini=(ROOT/'examples/hello-desktop/tweak.ini').read_text()
        self.lib=(ROOT/'tests/example-tweak/build/libexample-tweak.so').read_bytes()
    def tearDown(self):self.env.stop();self.tmp.cleanup()
    def package(self,files):
        p=self.root/'test.xfce-tweak'
        with zipfile.ZipFile(p,'w') as z:
            for name,data in files.items():z.writestr(name,data)
        return p
    def good(self):return {'tweak.ini':self.ini,'libhello-desktop.so':self.lib}
    def test_import_and_discover(self):
        item=import_package(self.package(self.good()),[self.modules])
        self.assertEqual(item['id'],'hello-desktop');self.assertFalse(item['defaultenabled'])
        self.assertEqual(len(discover([self.modules])),1)
    def test_traversal_rejected(self):
        files=self.good();files['../outside']='bad'
        with self.assertRaises(ValueError):import_package(self.package(files),[self.modules])
        self.assertFalse((self.root/'outside').exists())
    def test_absolute_path_rejected(self):
        files=self.good();files['/outside']='bad'
        with self.assertRaises(ValueError):import_package(self.package(files),[self.modules])
    def test_duplicate_id_rejected(self):
        p=self.package(self.good());import_package(p,[self.modules])
        with self.assertRaises(ValueError):import_package(p,[self.modules])
    def test_wrong_abi_rejected(self):
        files=self.good();files['tweak.ini']=self.ini.replace('ABI=1','ABI=999')
        with self.assertRaises(ValueError):import_package(self.package(files),[self.modules])
    def test_library_path_rejected(self):
        files=self.good();files['tweak.ini']=self.ini.replace('Library=lib','Library=../lib')
        with self.assertRaises(ValueError):import_package(self.package(files),[self.modules])
    def test_non_elf_rejected(self):
        files=self.good();files['libhello-desktop.so']=b'not an ELF'
        with self.assertRaises(ValueError):import_package(self.package(files),[self.modules])
    def test_symlink_rejected(self):
        p=self.package(self.good())
        with zipfile.ZipFile(p,'a') as z:
            info=zipfile.ZipInfo('link');info.external_attr=(0o120777<<16);z.writestr(info,'outside')
        with self.assertRaises(ValueError):import_package(p,[self.modules])
    def test_failed_import_leaves_no_partial_module(self):
        files=self.good();files['libhello-desktop.so']=b'bad'
        with self.assertRaises(ValueError):import_package(self.package(files),[self.modules])
        self.assertEqual(list(self.modules.iterdir()),[])
if __name__=='__main__':unittest.main(verbosity=2)
