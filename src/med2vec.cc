/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "med2vec.h"

#include <math.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fasttext {

void FastText::getVector(Vector& vec, const std::string& word) {
  const std::vector<int32_t>& ngrams = dict_->getNgrams(word);
  vec.zero();
  for (auto it = ngrams.begin(); it != ngrams.end(); ++it) {
    vec.addRow(*input_, *it);
  }
  if (ngrams.size() > 0) {
    vec.mul(1.0 / ngrams.size());
  }
}

void FastText::getVector(Vector& vec, const int32_t wordID) {
  vec.zero();
  vec.addRow(*input_, wordID);
}

void FastText::saveVectors() {
  std::ofstream ofs(args_->output + ".vec");
  if (!ofs.is_open()) {
    std::cout << "Error opening file for saving vectors." << std::endl;
    exit(EXIT_FAILURE);
  }
  ofs << dict_->nwords() << " " << args_->dim << std::endl;
  Vector vec(args_->dim);
  for (int32_t i = 0; i < dict_->nwords(); i++) {
    std::string word = dict_->getWord(i);
    // getVector(vec, word);
    getVector(vec, i);
    ofs << word << " " << vec << std::endl;
  }
  ofs.close();
}

void FastText::saveModel() {
  std::ofstream ofs(args_->output + ".bin", std::ofstream::binary);
  if (!ofs.is_open()) {
    std::cerr << "Model file cannot be opened for saving!" << std::endl;
    exit(EXIT_FAILURE);
  }
  args_->save(ofs);
  dict_->save(ofs);
  input_->save(ofs);
  output_->save(ofs);
  th_->save(ofs);
  ofs.close();
}

void FastText::loadModel(const std::string& filename) {
  std::ifstream ifs(filename, std::ifstream::binary);
  if (!ifs.is_open()) {
    std::cerr << "Model file cannot be opened for loading!" << std::endl;
    exit(EXIT_FAILURE);
  }
  loadModel(ifs);
  ifs.close();
}

void FastText::loadModel(std::istream& in) {
  args_ = std::make_shared<Args>();
  dict_ = std::make_shared<Dictionary>(args_);
  input_ = std::make_shared<Matrix>();
  output_ = std::make_shared<Matrix>();
  args_->load(in);
  dict_->load(in);
  input_->load(in);
  output_->load(in);
  th_->load(in);
  model_ = std::make_shared<Model>(input_, output_, th_, nCtxt_, args_, 0);
  if (args_->model == model_name::sup) {
    model_->setTargetCounts(dict_->getCounts(entry_type::label));
  } else {
    model_->setTargetCounts(dict_->getCounts(entry_type::word));
  }
}

void FastText::printInfo(real progress, real loss) {
  real t = real(clock() - start) / CLOCKS_PER_SEC;
  real wst = real(tokenCount) / t;
  real lr = args_->lr * (1.0 - progress);
  int eta = int(t / progress * (1 - progress) / args_->thread);
  int etah = eta / 3600;
  int etam = (eta - etah * 3600) / 60;
  std::cout << std::fixed;
  std::cout << "\rProgress: " << std::setprecision(1) << 100 * progress << "%";
  std::cout << "  words/sec/thread: " << std::setprecision(0) << wst;
  std::cout << "  lr: " << std::setprecision(6) << lr;
  std::cout << "  loss: " << std::setprecision(6) << loss;
  std::cout << "  eta: " << etah << "h" << etam << "m ";
  std::cout << std::flush;
}

void FastText::cbow(Model& model, real lr, const std::vector<int32_t>& line) {
  std::vector<int32_t> bow;
  std::uniform_int_distribution<> uniform(1, args_->ws);
  for (int32_t w = 0; w < line.size(); w++) {
    int32_t boundary = uniform(model.rng);
    bow.clear();
    for (int32_t c = -boundary; c <= boundary; c++) {
      if (c != 0 && w + c >= 0 && w + c < line.size()) {
        const std::vector<int32_t>& ngrams = dict_->getNgrams(line[w + c]);
        bow.insert(bow.end(), ngrams.cbegin(), ngrams.cend());
      }
    }
    model.update(bow, line[w], lr);
  }
}

int32_t FastText::get_th_idx_week(int32_t dst) {
  int32_t thidx = 0;
  if (dst < -26) {
    thidx = 0;
  } else if (dst >= -26 && dst < -4) {
    thidx = 1;
  } else if (dst >= -4 && dst < -1) {
    thidx = 2;
  } else if (dst == -1) {
    thidx = 3;
  } else if (dst == 0) {
    thidx = 4;
  } else if (dst == 1) {
    thidx = 5;
  } else if (dst > 1 && dst <= 4) {
    thidx = 6;
  } else if (dst > 4 && dst <= 26) {
    thidx = 7;
  } else {
    thidx = 8;
  }
  return thidx;
}

int32_t FastText::get_th_idx_day(int32_t dst) {
  int32_t thidx = 0;
  if (dst < -365) {
    thidx = 0;
  } else if (dst >= -365 && dst < -30) {
    thidx = 1;
  } else if (dst >= -30 && dst < -7) {
    thidx = 2;
  } else if (dst >= -7 && dst < -1) {
    thidx = 3;
  } else if (dst == -1) {
    thidx = 4;
  } else if (dst == 0) {
    thidx = 5;
  } else if (dst == 1) {
    thidx = 6;
  } else if (dst > 1 && dst <= 7) {
    thidx = 7;
  } else if (dst > 7 && dst <= 30) {
    thidx = 8;
  } else if (dst > 30 && dst <= 365) {
    thidx = 9;
  } else {
    thidx = 10;
  }
  return thidx;
}

// line is a set of visits for one patient
void FastText::sgContext(Model& model, real lr,
                         const std::vector<word_time>& line) {
  for (int32_t v = 0; v < line.size(); v++) {
    if (line[v].wordsID.size() == 0) continue;
    for (int32_t c = 0; c < line.size(); c++) {
      if (line[c].wordsID.size() == 0) continue;
      if (c == v && line[c].wordsID.size() == 1) continue;
      int32_t dst = line[c].time - line[v].time;
      int32_t thidx = -1;
      if (args_->timeUnit == time_unit::day) {
        thidx = get_th_idx_day(dst);
      } else {
        thidx = get_th_idx_week(dst);
      }
      for (int32_t i = 0; i < line[v].wordsID.size(); i++) {
        const std::vector<int32_t> inWord = {line[v].wordsID[i]};
        real theta = th_->getCell(inWord[0], thidx);
        std::srand((unsigned)std::time(0));
        // real pContext = 0.0;
        // int32_t a = std::abs(ws - thidx) + 1;
        // int32_t num = 0;
        for (int32_t k = 0; k < args_->nrand; k++) {
          int32_t j = std::rand() % line[c].wordsID.size();
          // for (int32_t j = 0; j < line[c].wordsID.size(); j++) {
          // std::cout << "j: " << j << std::endl;
          int32_t target = line[c].wordsID[j];
          if (target != inWord[0]) {
            // num++;
            model.update(inWord, target, lr, beta_a[thidx], beta_b[thidx],
                         inWord[0], thidx);
          }
        }
      }
    }
  }
}

void FastText::wordVectors() {
  std::string word;
  Vector vec(args_->dim);
  while (std::cin >> word) {
    getVector(vec, word);
    std::cout << word << " " << vec << std::endl;
  }
}

void FastText::printVectors() {
  wordVectors();
}

void FastText::trainThread(int32_t threadId) {
  std::ifstream ifs(args_->input);
  // should seek to the beginning of the line
  // utils::seek(ifs, threadId * utils::size(ifs) / args_->thread);
  utils::seekToBOS(ifs, threadId * utils::size(ifs) / args_->thread);

  Model model(input_, output_, attn_, bias_, args_, threadId);
  model.setTargetCounts(dict_->getCounts(entry_type::word));

  const int64_t ntokens = dict_->ntokens();
  int64_t localTokenCount = 0;
  // std::vector<int32_t> line, labels;
  std::vector<word_time> line;
  std::vector<int32_t> labels;
  while (tokenCount < args_->epoch * ntokens) {
    real progress = real(tokenCount) / (args_->epoch * ntokens);
    real lr = args_->lr * (1.0 - progress);
    localTokenCount += dict_->getLineContext(ifs, line, labels, model.rng);
    sgContext(model, lr, line);
    if (localTokenCount > args_->lrUpdateRate) {
      tokenCount += localTokenCount;
      localTokenCount = 0;
      if (threadId == 0 && args_->verbose > 1) {
        printInfo(progress, model.getLoss());
        // std::cout << "input l1 norm: " << input_->l1()
        //    << " theta l1 norm " << th_->l1()
        //    << " ouput l1 norm " << output_->l1()
        //    << std::endl;
      }
    }
  }
  if (threadId == 0 && args_->verbose > 0) {
    printInfo(1.0, model.getLoss());
    std::cout << std::endl;
  }
  ifs.close();
}

void FastText::loadVectors(std::string filename) {
  std::ifstream in(filename);
  std::vector<std::string> words;
  std::shared_ptr<Matrix> mat;  // temp. matrix for pretrained vectors
  int64_t n, dim;
  if (!in.is_open()) {
    std::cerr << "Pretrained vectors file cannot be opened!" << std::endl;
    exit(EXIT_FAILURE);
  }
  in >> n >> dim;
  if (dim != args_->dim) {
    std::cerr << "Dimension of pretrained vectors does not match -dim option"
              << std::endl;
    exit(EXIT_FAILURE);
  }
  mat = std::make_shared<Matrix>(n, dim);
  for (size_t i = 0; i < n; i++) {
    std::string word;
    in >> word;
    words.push_back(word);
    dict_->add(word);
    for (size_t j = 0; j < dim; j++) {
      in >> mat->data_[i * dim + j];
    }
  }
  in.close();

  dict_->threshold(1, 0);
  input_ =
      std::make_shared<Matrix>(dict_->nwords() + args_->bucket, args_->dim);
  input_->uniform(1.0 / args_->dim);

  for (size_t i = 0; i < n; i++) {
    int32_t idx = dict_->getId(words[i]);
    if (idx < 0 || idx >= dict_->nwords()) continue;
    for (size_t j = 0; j < dim; j++) {
      input_->data_[idx * dim + j] = mat->data_[i * dim + j];
    }
  }
}

void FastText::train(std::shared_ptr<Args> args) {
  args_ = args;
  dict_ = std::make_shared<Dictionary>(args_);
  if (args_->input == "-") {
    // manage expectations
    std::cerr << "Cannot use stdin for training!" << std::endl;
    exit(EXIT_FAILURE);
  }
  std::ifstream ifs(args_->input);
  if (!ifs.is_open()) {
    std::cerr << "Input file cannot be opened!" << std::endl;
    exit(EXIT_FAILURE);
  }
  dict_->readFromFile(ifs);
  ifs.close();

  if (args_->pretrainedVectors.size() != 0) {
    loadVectors(args_->pretrainedVectors);
  } else {
    // initialize input with an uniform distribution
    input_ = std::make_shared<Matrix>(dict_->nwords(), args_->dim);
    input_->uniform(1.0 / args_->dim);
  }

  if (args_->model == model_name::sup) {
    output_ = std::make_shared<Matrix>(dict_->nlabels(), args_->dim);
  } else {
    output_ = std::make_shared<Matrix>(dict_->nwords(), args_->dim);
  }
  output_->zero();

  // initialize matrix of theta
  if (args_->timeUnit == time_unit::day) {
    ws = 5;
  } else {
    ws = 4;
  }
  std::cout << "ws: " << ws << std::endl;
  th_ = std::make_shared<Matrix>(dict_->nwords(), 2 * ws + 1);
  int i = 0;

  start = clock();
  tokenCount = 0;
  std::vector<std::thread> threads;
  for (int32_t i = 0; i < args_->thread; i++) {
    threads.push_back(std::thread([=]() { trainThread(i); }));
  }
  for (auto it = threads.begin(); it != threads.end(); ++it) {
    it->join();
  }
  model_ = std::make_shared<Model>(input_, output_, th_, nCtxt_, args_, 0);

  saveModel();
  if (args_->model != model_name::sup) {
    saveVectors();
  }
}
}