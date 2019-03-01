{
  "targets": [{
    "target_name": "fuse_example",
    "include_dirs": [
      "<!(node -e \"require('napi-macros')\")",
      "<!(node -e \"require('fuse-shared-library/include')\")",
    ],
    "libraries": [
      "<!(node -e \"require('fuse-shared-library/lib')\")",
    ],
    "sources": [
      "binding.cc"
    ]
  }, {
    "target_name": "postinstall",
    "type": "none",
    "dependencies": ["fuse_example"],
    "copies": [{
      "destination": "build/Release",
      "files": [ "<!(node -e \"require('fuse-shared-library/lib')\")" ],
    }]
  }]
}
