# Headless native UI checks

From the repository root:

```sh
python3 Tests/native-ui/run.py
python3 Tests/native-ui/audit_binary.py /path/to/gta_sa.exe
```

The first command runs the portable C++ model under AddressSanitizer and
UndefinedBehaviorSanitizer, executes the actual lab server protocol with mocked
MTA event transport/time, and parses all lab Lua scripts. It requires a C++17
compiler and Lua. It never launches GTA or uses computer automation.

`model.cpp` also builds as a standalone console executable with MSVC C++17.
Use the VM-local source copy and a Developer Command Prompt, for example:

```bat
cl /std:c++17 /EHsc /W4 /IClient\sdk /IClient\game_sa Tests\native-ui\model.cpp /Fe:Build\native-ui-model.exe /Fo:Build\native-ui-model.obj
Build\native-ui-model.exe
```

The PE audit verifies the installed hook callsites, native timer renderer,
menu/text capacities and the retail grid-accept bounds defect. It explicitly
distinguishes the VM's relocated SCM allocator routines, which this adapter does
not call. These checks do not execute native rendering, prove live hooks, or
certify visual/input behavior.

The manual scenarios and lifecycle matrix are in
[test-resources/native-ui-test](../../test-resources/native-ui-test/README.md).
