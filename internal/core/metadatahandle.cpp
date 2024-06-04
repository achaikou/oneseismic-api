#include "metadatahandle.hpp"

#include <stdexcept>
#include <list>
#include <utility>
#include <boost/algorithm/string/join.hpp>
#include <OpenVDS/KnownMetadata.h>

#include "axis.hpp"
#include "boundingbox.hpp"
#include "direction.hpp"
#include "utils.hpp"

Axis make_single_cube_axis(
    OpenVDS::VolumeDataLayout const* const layout,
    int dimension
) {
    auto axis_descriptor = layout->GetAxisDescriptor(dimension);
    return Axis(
        axis_descriptor.GetCoordinateMin(),
        axis_descriptor.GetCoordinateMax(),
        axis_descriptor.GetNumSamples(),
        axis_descriptor.GetName(),
        axis_descriptor.GetUnit(),
        dimension
    );
}

SingleMetadataHandle::SingleMetadataHandle(OpenVDS::VolumeDataLayout const* const layout)
    : m_layout(layout),
      m_iline(make_single_cube_axis(layout, get_dimension({std::string(OpenVDS::KnownAxisNames::Inline())}))),
      m_xline(make_single_cube_axis(layout, get_dimension({std::string(OpenVDS::KnownAxisNames::Crossline())}))),
      m_sample(make_single_cube_axis(layout, get_dimension({std::string(OpenVDS::KnownAxisNames::Sample()), std::string(OpenVDS::KnownAxisNames::Depth()), std::string(OpenVDS::KnownAxisNames::Time())}))),
      m_coordinate_transformer(SingleCoordinateTransformer(OpenVDS::IJKCoordinateTransformer(layout)))
    {
    this->dimension_validation();

    if (this->m_iline.nsamples() < 2) {
        throw std::runtime_error(
            "Unsupported VDS, expect at least two inLines, got " +
            std::to_string(this->m_iline.nsamples())
        );
    }

    if (this->m_xline.nsamples() < 2) {
        throw std::runtime_error(
            "Unsupported VDS, expect at least two crossLines, got " +
            std::to_string(this->m_xline.nsamples())
        );
    }

    if (this->m_sample.nsamples() < 2) {
        throw std::runtime_error(
            "Unsupported VDS, expect at least two samples, got " +
            std::to_string(this->m_sample.nsamples())
        );
    }
}

Axis SingleMetadataHandle::iline() const noexcept(true) {
    return this->m_iline;
}

Axis SingleMetadataHandle::xline() const noexcept(true) {
    return this->m_xline;
}

Axis SingleMetadataHandle::sample() const noexcept(true) {
    return this->m_sample;
}

Axis SingleMetadataHandle::get_axis(
    Direction const direction
) const noexcept(false) {
    if (direction.is_iline())
        return this->iline();
    else if (direction.is_xline())
        return this->xline();
    else if (direction.is_sample())
        return this->sample();

    throw std::runtime_error("Unhandled axis");
}

Axis SingleMetadataHandle::get_axis(int dimension) const noexcept(false) {
    if (this->iline().dimension() == dimension)
        return this->iline();
    else if (this->xline().dimension() == dimension)
        return this->xline();
    else if (this->sample().dimension() == dimension)
        return this->sample();

    throw std::runtime_error("Unhandled dimension");
}

BoundingBox SingleMetadataHandle::bounding_box() const noexcept(false) {
    return BoundingBox(
        this->iline().nsamples(),
        this->xline().nsamples(),
        this->coordinate_transformer()
    );
}

std::string SingleMetadataHandle::crs() const noexcept(false) {
    auto const crs = OpenVDS::KnownMetadata::SurveyCoordinateSystemCRSWkt();
    return this->m_layout->GetMetadataString(crs.GetCategory(), crs.GetName());
}

std::string SingleMetadataHandle::input_filename() const noexcept(false) {
    auto const disp_name = OpenVDS::KnownMetadata::ImportInformationInputFileName();
    return this->m_layout->GetMetadataString(disp_name.GetCategory(), disp_name.GetName());
}

std::string SingleMetadataHandle::import_time_stamp() const noexcept(false) {
    auto const time_stamp = OpenVDS::KnownMetadata::ImportInformationImportTimeStamp();
    return this->m_layout->GetMetadataString(time_stamp.GetCategory(), time_stamp.GetName());
}

SingleCoordinateTransformer const& SingleMetadataHandle::coordinate_transformer() const noexcept(false) {
    return this->m_coordinate_transformer;
}

void SingleMetadataHandle::dimension_validation() const {
    if (this->m_layout->GetDimensionality() != 3) {
        throw std::runtime_error(
            "Unsupported VDS, expected 3 dimensions, got " +
            std::to_string(this->m_layout->GetDimensionality())
        );
    }
}

int SingleMetadataHandle::get_dimension(std::vector<std::string> const& names) const {
    for (auto i = 0; i < this->m_layout->GetDimensionality(); i++) {
        std::string dimension_name = this->m_layout->GetDimensionName(i);
        if (std::find(names.begin(), names.end(), dimension_name) != names.end()) {
            return i;
        }
    }
    throw std::runtime_error(
        "Requested axis not found under names " + boost::algorithm::join(names, ", ") +
        " in vds file "
    );
}

Axis make_double_cube_axis(
    SingleMetadataHandle const* const metadata_a,
    SingleMetadataHandle const* const metadata_b,
    int dimension
    ) {
        auto axis_a = metadata_a->get_axis(dimension);
        auto axis_b = metadata_b->get_axis(dimension);

        if (axis_a.name() != axis_b.name()) {
            std::string args = axis_a.name() + " versus " + axis_b.name();
            throw detail::bad_request("Dimension name mismatch for dimension " + std::to_string(dimension) + ": " + args);
        }

        if (axis_a.unit() != axis_b.unit()) {
            std::string args = axis_a.unit() + " versus " + axis_b.unit();
            throw detail::bad_request("Dimension unit mismatch for axis " + axis_a.name() + ": " + args);
        }

        if (axis_a.stepsize() != axis_b.stepsize()) {
            std::string args = utils::to_string_with_precision(axis_a.stepsize()) + " versus " +
                               utils::to_string_with_precision(axis_b.stepsize());
            throw detail::bad_request("Stepsize mismatch in axis " + axis_a.name() + ": " + args);
        }

        auto min = std::max(axis_a.min(), axis_b.min());
        auto max = std::min(axis_a.max(), axis_b.max());

        auto nsamples = 1 + ((max - min) / (int) axis_a.stepsize());

        return Axis(min, max, nsamples, axis_a.name(), axis_a.unit(), dimension);
}

DoubleMetadataHandle::DoubleMetadataHandle(
    OpenVDS::VolumeDataLayout const* const layout_a,
    OpenVDS::VolumeDataLayout const* const layout_b,
    SingleMetadataHandle const* const metadata_a,
    SingleMetadataHandle const* const metadata_b,
    enum binary_operator binary_symbol
)
    : m_layout(DoubleVolumeDataLayout(layout_a, layout_b)),
      m_metadata_a(metadata_a),
      m_metadata_b(metadata_b),
      m_binary_symbol(binary_symbol),
      m_iline(make_double_cube_axis(metadata_a, metadata_b, get_dimension({std::string(OpenVDS::KnownAxisNames::Inline())}))),
      m_xline(make_double_cube_axis(metadata_a, metadata_b, get_dimension({std::string(OpenVDS::KnownAxisNames::Crossline())}))),
      m_sample(make_double_cube_axis(metadata_a, metadata_b, get_dimension({std::string(OpenVDS::KnownAxisNames::Sample()), std::string(OpenVDS::KnownAxisNames::Depth()), std::string(OpenVDS::KnownAxisNames::Time())}))),
      m_coordinate_transformer(m_metadata_a->coordinate_transformer(), m_metadata_b->coordinate_transformer()) {
    this->dimension_validation();

    if (this->m_iline.nsamples() < 2) {
        throw std::runtime_error(
            "Unsupported VDS pair, expect that the intersection contains at least two inLines, got " +
            std::to_string(this->m_iline.nsamples())
        );
    }

    if (this->m_xline.nsamples() < 2) {
        throw std::runtime_error(
            "Unsupported VDS pair, expect that the intersection contains at least two crossLines, got " +
            std::to_string(this->m_xline.nsamples())
        );
    }

    if (this->m_sample.nsamples() < 2) {
        throw std::runtime_error(
            "Unsupported VDS pair, expect that the intersection contains at least two samples, got " +
            std::to_string(this->m_sample.nsamples())
        );
    }
}

Axis DoubleMetadataHandle::iline() const noexcept(true) {
    return this->m_iline;
}

Axis DoubleMetadataHandle::xline() const noexcept(true) {
    return this->m_xline;
}

Axis DoubleMetadataHandle::sample() const noexcept(true) {
    return this->m_sample;
}

Axis DoubleMetadataHandle::get_axis(
    Direction const direction
) const noexcept(false) {
    if (direction.is_iline())
        return this->iline();
    else if (direction.is_xline())
        return this->xline();
    else if (direction.is_sample())
        return this->sample();

    throw std::runtime_error("Unhandled axis");
}

BoundingBox DoubleMetadataHandle::bounding_box() const noexcept(false) {
    return BoundingBox(
        this->iline().nsamples(),
        this->xline().nsamples(),
        this->coordinate_transformer()
    );
}

std::string DoubleMetadataHandle::crs() const noexcept(false) {
    // DoubleVolumeDataLayout constructor ensures that 'crs' for A and B are identical.
    return this->m_metadata_a->crs();
}

std::string DoubleMetadataHandle::input_filename() const noexcept(false) {
    return this->m_metadata_a->input_filename() + this->operator_string() + this->m_metadata_b->input_filename();
}

std::string DoubleMetadataHandle::import_time_stamp() const noexcept(false) {
    return this->m_metadata_a->import_time_stamp() + this->operator_string() + this->m_metadata_b->import_time_stamp();
}

DoubleCoordinateTransformer const& DoubleMetadataHandle::coordinate_transformer() const noexcept(false) {
    return this->m_coordinate_transformer;
}

void DoubleMetadataHandle::dimension_validation() const {
    if (this->m_layout.GetDimensionality() != 3) {
        throw std::runtime_error(
            "Unsupported VDS, expected 3 dimensions, got " +
            std::to_string(this->m_layout.GetDimensionality())
        );
    }
}

int DoubleMetadataHandle::get_dimension(std::vector<std::string> const& names) const {
    for (auto i = 0; i < this->m_layout.GetDimensionality(); i++) {
        std::string dimension_name = this->m_layout.GetDimensionName(i);
        if (std::find(names.begin(), names.end(), dimension_name) != names.end()) {
            return i;
        }
    }
    throw std::runtime_error(
        "Requested axis not found under names " + boost::algorithm::join(names, ", ") +
        " in vds file "
    );
}

std::string DoubleMetadataHandle::operator_string() const noexcept(false) {

    switch (this->m_binary_symbol) {
        case binary_operator::NO_OPERATOR:
            return " ? ";

        case binary_operator::ADDITION:
            return " + ";

        case binary_operator::SUBTRACTION:
            return " - ";

        case binary_operator::MULTIPLICATION:
            return " * ";

        case binary_operator::DIVISION:
            return " / ";

        default:
            return " XX ";
    }
}

void DoubleMetadataHandle::offset_samples_to_match_cube_a(voxel const* samples, std::size_t const nsamples, std::vector<float>* samples_a) noexcept(true) {

    auto data = samples_a->data();
    for (int v = 0; v < nsamples; v++) {
        for (int i = 0; i < this->m_layout.Dimensionality_Max; i++) {
            data[v * this->m_layout.Dimensionality_Max + i] = samples[v][i] + this->m_layout.GetDimensionIndexOffset_a(i);
        }
    }
}

void DoubleMetadataHandle::offset_samples_to_match_cube_b(voxel const* samples, std::size_t const nsamples, std::vector<float>* samples_b) noexcept(true) {

    auto data = samples_b->data();
    for (int v = 0; v < nsamples; v++) {
        for (int i = 0; i < this->m_layout.Dimensionality_Max; i++) {
            data[v * this->m_layout.Dimensionality_Max + i] = samples[v][i] + this->m_layout.GetDimensionIndexOffset_b(i);
        }
    }
}
