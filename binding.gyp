{
    "targets":[
        {
            "target_name":"raspberrysensor",
            "sources":[
                "src/raspberrysensor.cc"
            ],
            "libraries": [
              "-lrt",
              "-lbcm2835"
            ]
        }
    ]
}
