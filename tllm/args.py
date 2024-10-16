import yaml
import json

from dataclasses import dataclass


@dataclass
class BuildConfig:
    # The input model directory
    model_dir: str
    # The temp model directory to save checkpoint, default is to add -temp to input path
    temp_dir: str = None
    # The output directory, the recommended setting is None to get formatted output name
    output_dir: str = None
    # The model type, default is llama
    model_type: str = "llama"
    # Whether remove temp directory, default is false
    remove_temp_dir: bool = False
    # The tensor parallel size of generated engines
    tp_size: int = 1
    # The data type of generated engines, choices is ["fp", "f8", "w4", "w8", "a8"]
    dtype: str = "float16"
    # The max batch size of generated engines
    max_batch_size: int = 32
    # The max input length of generated engines
    max_input_len: int = 1024
    # The max output len of generated engines
    max_output_len: int = 512
    # The max num tokens of generated engines, a suitable value can be determined through calculation or experimentation
    max_num_tokens: int = -1
    # Whether build engines with prompt cache
    use_prompt_cache: bool = True


def get_args(yml_path: str):
    with open(yml_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    print("=" * 40)
    print(f"config:\n{json.dumps(data, ensure_ascii=False, indent=4)}")
    print("=" * 40)
    args = BuildConfig(**data)

    return args
