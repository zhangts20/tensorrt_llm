#include <filesystem>
#include <fstream>
#include <mpi/mpi.h>
#include <thread>
#include <vector>

#include "cxxopts.hpp"
#include "nlohmann/json.hpp"
#include "tensorrt_llm/common/logger.h"
#include "tensorrt_llm/executor/executor.h"
#include "tensorrt_llm/plugins/api/tllmPlugin.h"

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace tlc = tensorrt_llm::common;
namespace tle = tensorrt_llm::executor;

void newRequests(std::vector<tle::Request> &requests) {
    tle::VecTokens vec_tokens = {1, 1724, 338, 21784, 29257, 29973};
    tle::SizeType32 max_new_tokens = 17;
    tle::OutputConfig output_config = tle::OutputConfig(
        /* returnLogProbs =*/false,
        /* returnContextLogits =*/false,
        /* returnGenerationLogits =*/false,
        /* excludeInputFromOutput =*/false,
        /* returnEncoderOutput =*/false);
    tle::SamplingConfig sampling_config = tle::SamplingConfig(
        /* beamWidth =*/1,
        /* topK =*/std::nullopt,
        /* topP =*/std::nullopt,
        /* topPMin =*/std::nullopt,
        /* topPResetIds =*/std::nullopt,
        /* topPDecay =*/std::nullopt,
        /* seed =*/std::nullopt,
        /* temperature =*/std::nullopt,
        /* minTokens =*/std::nullopt,
        /* beamSearchDiversityRate =*/std::nullopt,
        /* repetitionPenalty =*/std::nullopt,
        /* presencePenalty =*/std::nullopt,
        /* frequencyPenalty =*/std::nullopt,
        /* lengthPenalty =*/std::nullopt,
        /* earlyStopping =*/std::nullopt,
        /* noRepeatNgramSize =*/std::nullopt);

    for (size_t i = 0; i < 8; ++i) {
        requests.push_back(tle::Request(
            /* inputTokenIds =*/vec_tokens,
            /* maxTokens =*/max_new_tokens + i,
            /* streaming =*/true,
            /* samplingConfig =*/sampling_config,
            /* outputConfig =*/output_config,
            /* endId =*/std::nullopt,
            /* padId =*/std::nullopt,
            /* positionIds =*/std::nullopt,
            /* badWords =*/std::nullopt,
            /* stopWords =*/std::nullopt,
            /* embeddingBias =*/std::nullopt,
            /* externalDraftTokensConfig =*/std::nullopt,
            /* pTuningConfig =*/std::nullopt,
            /* loraConfig =*/std::nullopt,
            /* lookaheadConfig =*/std::nullopt,
            /* logitsPostProcessorName =*/std::nullopt,
            /* encoderInputTokenIds =*/std::nullopt,
            /* clientId =*/std::nullopt,
            /* returnAllGeneratedTokens =*/false,
            /* priority =*/tle::Request::kDefaultPriority,
            /* type =*/tle::RequestType::REQUEST_TYPE_CONTEXT_AND_GENERATION,
            /* contextPhaseParams =*/std::nullopt,
            /* encoderInputFeatures =*/std::nullopt,
            /* encoderOutputLength =*/std::nullopt,
            /* numReturnSequences =*/1));
    }
}

std::vector<tle::IdType> addRequests(tle::Executor &executor,
                                     std::vector<tle::Request> &requests) {
    std::vector<tle::IdType> request_ids;
    for (size_t i = 0; i < requests.size(); ++i) {
        if (executor.canEnqueueRequests()) {
            request_ids.push_back(
                executor.enqueueRequest(std::move(requests[i])));
        }
    }

    return request_ids;
}

void setValue(tle::ExecutorConfig &executor_config,
              const fs::path &engine_dir) {
    fs::path config_path = engine_dir / "config.json";
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "open config file failed" << std::endl;
        exit(-1);
    }

    json config;
    config_file >> config;
    // Read value from config
    tle::SizeType32 max_beam_width = config["build_config"]["max_beam_width"];
    tle::SizeType32 max_batch_size = config["build_config"]["max_batch_size"];
    tle::SizeType32 max_num_tokens = config["build_config"]["max_num_tokens"];
    executor_config.setMaxBeamWidth(max_beam_width);
    executor_config.setMaxBatchSize(max_batch_size);
    executor_config.setMaxNumTokens(max_num_tokens);
}

int main(int argc, char **argv, char **envp) {
    // clang-format off
    cxxopts::Options options("MAIN", "A cpp inference of TensorRT-LLM.");
    options.add_options()("help", "Print help");
    options.add_options()("model_dir", "The input engine directory.", cxxopts::value<std::string>());
    options.add_options()("log_level", "The log level.", cxxopts::value<std::string>()->default_value("info"));
    // clang-format on
    auto args = options.parse(argc, argv);

    if (args.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (!args.count("model_dir")) {
        std::cout << "The model dir is not given." << std::endl;
        return 1;
    }
    fs::path engine_dir = args["model_dir"].as<std::string>();
    if (!fs::exists(engine_dir)) {
        std::cout << "The model dir does not exist." << std::endl;
        return 1;
    }

    auto logger = tlc::Logger::getLogger();
    auto const log_level = args["log_level"].as<std::string>();
    if (log_level == "trace") {
        logger->setLevel(tlc::Logger::TRACE);
    } else if (log_level == "debug") {
        logger->setLevel(tlc::Logger::DEBUG);
    } else if (log_level == "info") {
        logger->setLevel(tlc::Logger::INFO);
    } else if (log_level == "warning") {
        logger->setLevel(tlc::Logger::WARNING);
    } else if (log_level == "error") {
        logger->setLevel(tlc::Logger::ERROR);
    } else {
        std::cout << "Unexpected log level: " + log_level << std::endl;
        return 1;
    }

    initTrtLlmPlugins();

#ifdef DEBUG_ENV
    for (char **env = envp; *env != 0; env++) {
        char *thisEnv = *env;
        printf("%s\n", thisEnv);
    }
#endif

    // Print tensor parallel info
    const char *world_rank_ = std::getenv("OMPI_COMM_WORLD_RANK");
    int world_rank = 0;
    if (world_rank_ != nullptr) {
        world_rank = std::stoi(world_rank_);
    }
    const char *world_size_ = std::getenv("OMPI_COMM_WORLD_SIZE");
    int world_size = 1;
    if (world_size_ != nullptr) {
        world_size = std::stoi(world_size_);
    }
    std::cout << "Process " << world_rank << " of " << world_size << std::endl;

    static fs::path ENGINE_DIR = "/data/zhangtaoshan/models/llm/trtllm_0140/"
                                 "llama2-7b-tp1-float16-wcache";
    tle::ExecutorConfig executor_config;
    setValue(executor_config, engine_dir);

    tle::SchedulerConfig scheduler_config(
        /* capacitySchedulerPolicy =*/tle::CapacitySchedulerPolicy::
            kGUARANTEED_NO_EVICT,
        /* contextChunkingPolicy =*/tle::ContextChunkingPolicy::
            kFIRST_COME_FIRST_SERVED);
    executor_config.setSchedulerConfig(scheduler_config);

    tle::KvCacheConfig kv_cache_config(
        /* enableBlockReuse =*/false,
        /* maxTokens =*/std::nullopt,
        /* maxAttentionWindowVec =*/std::nullopt,
        /* sinkTokenLength =*/std::nullopt,
        /* freeGpuMemoryFraction =*/std::nullopt,
        /* hostCacheSize =*/std::nullopt,
        /* onboardBlocks =*/true,
        /* crossKvCacheFraction =*/std::nullopt);
    executor_config.setKvCacheConfig(kv_cache_config);

    executor_config.setEnableChunkedContext(/* enableChunkedContext =*/false);
    executor_config.setNormalizeLogProbs(/* normalizeLogProbs =*/false);
    executor_config.setIterStatsMaxIterations(
        /* iterStatsMaxIterations =*/tle::kDefaultIterStatsMaxIterations);
    executor_config.setRequestStatsMaxIterations(
        /* requestStatsMaxIterations =*/tle::kDefaultRequestStatsMaxIterations);
    executor_config.setBatchingType(
        /* batchingType =*/tle::BatchingType::kINFLIGHT);

    tle::ParallelConfig parallel_config(
        /* commType =*/tle::CommunicationType::kMPI,
        /* commMode =*/tle::CommunicationMode::kLEADER,
        /* deviceIds =*/std::nullopt,
        /* participantIds =*/std::nullopt,
        /* orchestratorConfig =*/std::nullopt);
    executor_config.setParallelConfig(parallel_config);

    tle::PeftCacheConfig peft_cache_config(
        /* numHostModuleLayer =*/0,
        /* numDeviceModuleLayer =*/0,
        /* optimalAdapterSize =*/8,
        /* maxAdapterSize =*/64,
        /* numPutWorkers =*/1,
        /* numEnsureWorkers =*/1,
        /* numCopyStreams =*/1,
        /* maxPagesPerBlockHost =*/24,
        /* maxPagesPerBlockDevice =*/8,
        /* deviceCachePercent =*/std::nullopt,
        /* hostCacheSize =*/std::nullopt);
    executor_config.setPeftCacheConfig(peft_cache_config);

    tle::LogitsPostProcessorConfig logits_post_processor_config(
        /* processorMap =*/std::nullopt,
        /* processorBatched =*/std::nullopt,
        /* replicate =*/true);
    executor_config.setLogitsPostProcessorConfig(logits_post_processor_config);

    tle::DecodingConfig decoding_config(
        /* decodingMode =*/std::nullopt,
        /* lookaheadDecodingConfig =*/std::nullopt,
        /* medusaChoices =*/std::nullopt);
    executor_config.setDecodingConfig(decoding_config);

    executor_config.setGpuWeightsPercent(/* gpuWeightsPercent =*/1);
    executor_config.setMaxQueueSize(/* maxQueueSize =*/std::nullopt);

    tle::ExtendedRuntimePerfKnobConfig extended_runtime_perf_knob_config(
        /* multiBlockMode =*/true,
        /* enableContextFMHAFP32Acc =*/false);
    executor_config.setExtendedRuntimePerfKnobConfig(
        extended_runtime_perf_knob_config);

#ifdef DEBUG_TLLM
    tle::DebugConfig debug_config(
        /* dumpInputTensors =*/false,
        /* dumpOuputTensors =*/false,
        /* debugTensorNames =*/{});
    executor_config.setDebugConfig(debug_config);
#endif

    executor_config.setRecvPollPeriodMs(/* recvPollPeriodMs =*/0);
    executor_config.setMaxSeqIdleMicroseconds(
        /* maxSeqIdleMicroseconds =*/180000000);

    tle::SpeculativeDecodingConfig speculative_decoding_config(
        /* fastLogits =*/false);
    executor_config.setSpecDecConfig(speculative_decoding_config);

    tle::Executor executor = tle::Executor(
        /* modelPath =*/ENGINE_DIR,
        /* modelType =*/tle::ModelType::kDECODER_ONLY,
        /* executorConfig =*/executor_config);

    // Initialize requests
    std::vector<tle::Request> requests;
    newRequests(requests);
    std::vector<tle::IdType> request_ids = addRequests(executor, requests);

    std::chrono::milliseconds ms(5000);
    tle::SizeType32 numFinished{0};
    while (numFinished < request_ids.size()) {
        // Get results
        std::vector<tle::Response> responses =
            executor.awaitResponses(/* timeout =*/ms);
        // Loop for each response, if response is finished, print
        for (tle::Response response : responses) {
            if (response.hasError()) {
                printf("Error: %s\n",
                       std::to_string(response.getRequestId()).c_str());
            } else {
                tle::Result result = response.getResult();
                // Set beam width to 0
                tle::VecTokens output_tokens = result.outputTokenIds.at(0);
                printf("Output tokens: %s\n",
                       tlc::vec2str(output_tokens).c_str());
                tle::FinishReason finish_reason = result.finishReasons.at(0);
                printf("Finish reason: %d\n", finish_reason);
                if (result.isFinal) {
                    printf("Finish: %lu\n", response.getRequestId());
                    numFinished++;
                }
            }
        }
    }
    return 0;
}