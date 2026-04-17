# PyVectorSSD

Guide for Python-version VectorSSD's API usage

## Getting Started

### Prerequisites

Build C++ source code in advance at the `build` directory and make sure that the Cosmos+ OpenSSD has been mounted to the host.

### Running the tests

To operate the VectorSSD within Python program, see the following APIs which are defined at `vectorssd.h` and `vectorssd.cc`.

- `Open`(const std::string& dev)
- `Close`()
- `Put`(const std::string& key, const float vector[VECTOR_DIMENSION])
- `Get`(const std::string& key, std::string* value)
- `VectorSearch`(const float query_vector[VECTOR_DIMENSION], int top_k, VectorSearchReturn* rets)
- `VectorBuild`()

## Usage
```bash
python3 pyvectorssd.py --config config.json --type a 
```

## Example
See the below code or `tutorial_db.py` as an example.

```Python
mydb = vectorssd.DB()
mycollection = "temp_collection_name"
ret = mydb.open(dev, mycollection) # The dev is mount directory path
key = "xxx"
value = np.random.random(vectorssd.VECTOR_DIMENSION).astype(np.float32) # Random value
mydb.put(key, value) # Put
mydb.close()
```


