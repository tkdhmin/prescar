import sys
import vectorssd
import numpy as np

sys.path.append("../build")


def test_functionality():
    mydb = vectorssd.DB()
    collection_name = "temp"
    _ = mydb.open_mock("my_virtual_db", collection_name)
    test_key = "13"
    test_vector = np.random.random(vectorssd.VECTOR_DIMENSION).astype(np.float32)
    print(test_vector)
    mydb.put_mock(test_key, test_vector)
    mydb.close_mock()


if __name__ == "__main__":
    np.random.seed(5)
    test_functionality()
