// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "thrift_sender.h"
#include "udp_transport.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace jaeger
{

using namespace jaegertracing;

ThriftSender::ThriftSender(std::unique_ptr<Transport> &&transport)
    : transport_(std::move(transport)),
      protocol_factory_(new apache::thrift::protocol::TCompactProtocolFactory()),
      thrift_buffer_(new apache::thrift::transport::TMemoryBuffer())
{}

int ThriftSender::Append(std::unique_ptr<Recordable> &&span) noexcept
{
  if (span == nullptr)
  {
    return 0;
  }

  uint32_t max_span_bytes = transport_->MaxPacketSize() - kEmitBatchOverhead;
  if (process_.serviceName.empty())
  {
    process_.serviceName = span->ServiceName();

    process_bytes_size_ = CalcSizeOfSerializedThrift(process_);
    max_span_bytes -= process_bytes_size_;
  }

  thrift::Span &jaeger_span = *span->Span();
  jaeger_span.__set_tags(span->Tags());

  const uint32_t span_size = CalcSizeOfSerializedThrift(jaeger_span);
  if (span_size > max_span_bytes)
  {
    // TODO, log too large span error.
    return 0;
  }

  byte_buffer_size_ += span_size;
  if (byte_buffer_size_ <= max_span_bytes)
  {
    span_buffer_.push_back(jaeger_span);
    if (byte_buffer_size_ < max_span_bytes)
    {
      return 0;
    }
    else
    {
      // byte buffer is full so flush it before appending new span.
      return Flush();
    }
  }

  const auto flushed = Flush();
  span_buffer_.push_back(jaeger_span);
  byte_buffer_size_ = span_size + process_bytes_size_;

  return flushed;
}

int ThriftSender::Flush()
{
  if (span_buffer_.empty())
  {
    return 0;
  }

  thrift::Batch batch;
  batch.__set_process(process_);
  batch.__set_spans(span_buffer_);

  transport_->EmitBatch(batch);

  ResetBuffers();

  return static_cast<int>(batch.spans.size());
}

void ThriftSender::Close()
{
  Flush();
}

}  // namespace jaeger
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
