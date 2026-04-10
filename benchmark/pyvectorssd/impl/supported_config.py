from enum import Enum


class CheckableEnum(Enum):
    """List-interface enum class"""

    @classmethod
    def list(cls):
        return list(map(lambda c: c.value, cls))


class SupportedConfiguration(CheckableEnum):
    """Supported configuration fields"""

    COLLECTION_NAME = "collection_name"
    DIMENSION = "dimension"
    INDEX_TYPE = "index_type"
    METRIC_TYPE = "metric_type"
    INDEX_PARAMS = "index_params"
    SEARCH_PARAMS = "search_params"
    SIMULATION_DAYS = "simulation_days"
    TIME_SCALE_FACTOR = "time_scale_factor"
    VECTOR_FIELD = "vector_field"
    ID_FIELD = "id_field"
    METADATA_FIELDS = "metadata_fields"
    SCENARIOS = "scenarios"
