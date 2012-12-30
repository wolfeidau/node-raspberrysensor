{
    "targets":[
        {
            "target_name":"raspberrysensor",
            "sources":[
                "src/raspberrysensor_humidity.cc"
            ],
            "libraries": [
              "-lrt",
              "-lbcm2835"
            ]
        }
    ]
}
